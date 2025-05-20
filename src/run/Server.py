import argparse
import csv
import lzma
import pickle
import socket
import ssl
import os
import datetime
import sys
import time
import asyncio
import pylzma

import aioquic
from aioquic.asyncio import serve, QuicConnectionProtocol
from aioquic.asyncio.server import QuicServer
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.connection import QuicConnection


class Server:
    def __init__(self, host, certfile, keyfile, cafile, episode_iterations_max, port, compress, end_time_path):
        self.port = port
        self.host = host
        self.certfile = certfile
        self.keyfile = keyfile
        self.cafile = cafile
        self.episode_iterations_max = episode_iterations_max
        self.episode_iterations_current = 0
        self.quic_server: QuicServer = None
        self.compress: bool = compress
        self.end_time_path = end_time_path


    # The problem is most likely caused by an ack not received, therefore the episode_iterations_current
    # is not incremented and the loop is not broken. rewriting to store end_time in file instead
    async def __store_end_time_quic(self, end_time):

        # Sends both the current iteration and the end time
        payload = [self.episode_iterations_current, end_time]
        with open(self.end_time_path, "wb") as bf:
            bf.write(pickle.dumps(payload))
            bf.close()


    async def __receive_data_quic(self, reader, writer):
        print("New stream opened")
        data = b""

        while not reader.at_eof():
            chunk = await reader.read(1024)
            if not chunk:
                break
            data += chunk
        writer.write_eof()

        print(f"received size: {len(data) / 1000} kb")

        if self.compress:
            # Decompress the received data if compression is enabled
            pylzma.decompress(data)

        # save the end time to file
        end_time = datetime.datetime.now()
        await self.__store_end_time_quic(end_time)

        # iterates for each received file to tell the break function when to break the loop
        self.episode_iterations_current += 1

    # runs as a subprocesss to close the quic connection and loop
    async def __quic_break(self):
        while self.episode_iterations_current < self.episode_iterations_max:
            await asyncio.sleep(0.001)
        print(f"current i: {self.episode_iterations_current}")
        self.quic_server.close()
        loop = asyncio.get_running_loop()
        loop.stop()
        print("breaking loop")
        return

    def start_quic(self):

        loop = asyncio.get_event_loop()

        try:

            loop.run_until_complete(self.run_quic())
            asyncio.ensure_future(self.__quic_break())
            # Start the task to check the condition to break out asynchronously

            loop.run_forever()
        except KeyboardInterrupt:
            print("interrupted")
            pass
        finally:
            loop.close()
            # creates new event loop
            asyncio.set_event_loop(asyncio.new_event_loop())
            print("loop stopped")
            # nulls it
            self.episode_iterations_current = 0

    async def run_quic(self):
        configuration = QuicConfiguration(is_client=False)

        configuration.load_cert_chain(certfile=self.certfile, keyfile=self.keyfile)

        def handle_stream_awaited(reader, writer):
            asyncio.create_task(self.__receive_data_quic(reader, writer))

        self.quic_server = await serve(self.host, self.port, configuration=configuration, stream_handler=handle_stream_awaited)


    def __receive_data_tcp(self, server_socket):

        data_size_bytes = server_socket.recv(8)
        data_size = int.from_bytes(data_size_bytes, byteorder="big")

        received_size = 0
        data = b""  # Initialize data variable as bytes object
        while received_size < data_size:
            chunk = server_socket.recv(4096)
            received_size += len(chunk)
            data += chunk  # Accumulate received data

        print(f"received {received_size / 1000} kb")

        if self.compress:
            # Decompress the received data if compression is enabled
            pylzma.decompress(data)

        end_time = datetime.datetime.now()
        return end_time


    def run_tcp(self):
        episode_iterations = 1
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile=self.certfile, keyfile=self.keyfile)
        context.load_verify_locations(cafile=self.cafile)

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((self.host, self.port))
            s.listen(0)

            with context.wrap_socket(s, server_side=True) as server_socket:
                # proceed until episode iterations has been fulfilled
                while True:
                    connection, client_address = server_socket.accept()
                    with connection:

                        try:
                            end_time = self.__receive_data_tcp(connection)
                            #print(f"episode_iteration: {episode_iterations}")

                            # Send end time back to client
                            connection.sendall(pickle.dumps(end_time))
                            connection.close()
                            episode_iterations += 1
                        except ConnectionResetError:
                            print("Connection closed by client before receiving end time.")
                        except Exception as e:
                            print("An error occurred:", e)
                    if episode_iterations > self.episode_iterations_max:
                        break
