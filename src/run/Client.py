import argparse
import csv
import pickle
import socket
import ssl
import os
import datetime
import sys
import time
import asyncio
from aioquic.asyncio import connect
from aioquic.quic.configuration import QuicConfiguration

sys.path.append(os.path.dirname("/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src"))
from src.lib.Utils import Utils

class Client:
    def __init__(self, host, certfile, keyfile, cafile, port, end_time_path):
        self.last_iteration = 0
        self.port = port
        self.host = host
        self.certfile = certfile
        self.keyfile = keyfile
        self.cafile = cafile
        self.end_time_path = end_time_path

    async def __send_data_quic(self, reader, writer, buffer):
        ut = Utils
        byte_buffer = ut.chunk_string(Utils(), buffer, 1024)

        for chunk in byte_buffer:
            writer.write(chunk)
        writer.write_eof()

    def retrieve_end_time_quic(self, last_iteration_before, end_time_path):
        '''Stays in the loop until the read iteration is not the same as the stored last iteration.
         This ensures that the same time is not read twice incase the client reads from the file
         before the server has saved to it. '''
        end_time = None
        last_iteration = last_iteration_before
        while last_iteration_before == last_iteration:
            time.sleep(0.1)
            try:
                with open(end_time_path, "rb") as bf:
                    payload_bytes = bf.read()
                    if len(payload_bytes) == 0:
                        continue
                    else:
                        payload = pickle.loads(payload_bytes)
                        last_iteration = payload[0]
                        end_time = payload[1]
                        bf.close()
            except:
                continue
            if self.last_iteration == 0:
                break
        return end_time


    def start_quic(self, buffer) -> datetime:
        return asyncio.run(self.run_quic(buffer))

    async def run_quic(self, buffer) -> datetime:
        configuration = QuicConfiguration(is_client=True, congestion_control_algorithm='cubic')
        configuration.load_verify_locations(cafile=self.cafile)

        #start_time = datetime.datetime.now()
        async with connect(self.host, self.port, configuration=configuration) as protocol:
            await protocol.wait_connected()
            #print("Connected")
            reader, writer = await protocol.create_stream()

            await self.__send_data_quic(reader, writer, buffer)

            while not reader.at_eof():
                await reader.read(1024)

            end_time = self.retrieve_end_time_quic(last_iteration_before=self.last_iteration, end_time_path=self.end_time_path)

            protocol.close()
            await protocol.wait_closed()

            return end_time

    def __send_data_tcp(self, client_socket, buffer):
        ut = Utils()
        # send size of buffer
        client_socket.sendall(len(buffer).to_bytes(8, byteorder="big"))
        # send the data
        byte_buffer = ut.chunk_string(buffer, 4096)
        size = 0
        for chunk in byte_buffer:
            client_socket.sendall(chunk)
            size += len(chunk)


    def run_tcp(self, buffer) -> datetime:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.load_cert_chain(certfile=self.certfile,
                                keyfile=self.keyfile)
        context.load_verify_locations(cafile=self.cafile)


        with socket.create_connection((self.host, self.port)) as s:
            with context.wrap_socket(s, server_hostname=self.host) as client_socket:
                #print("Connection established: ")

                self.__send_data_tcp(client_socket, buffer)

                # Receive end time from server
                end_time_bytes = client_socket.recv(1024)

                end_time = pickle.loads(end_time_bytes)

                # Close connection after receiving end time
                client_socket.close()
                return end_time

