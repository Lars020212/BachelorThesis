# server.py
import argparse
import asyncio
import datetime
import pickle

from aioquic.asyncio import serve
from aioquic.quic.configuration import QuicConfiguration


async def handle_stream(reader, writer):
    print("New stream opened.")
    received_files = []
    while not reader.at_eof():
        data = await reader.read(1024)
        if not data:
            break
        received_files.append(data)
    size = 0
    for packet in received_files:
        size += len(packet)

    print(f"files received size {size / 1000} kB")

    # send back current time to client
    writer.write(pickle.dumps(datetime.datetime.now()))
    writer.write_eof()
    await writer.drain()
    writer.close()
    print("Server: DONE")



async def run_server(host, port, certfile, keyfile):
    configuration = QuicConfiguration(is_client=False)

    configuration.load_cert_chain(certfile=certfile, keyfile=keyfile)

    def handle_stream_awaited(reader, writer):
        asyncio.create_task(handle_stream(reader, writer))
    await serve(host, port, configuration=configuration, stream_handler=handle_stream_awaited)



def main(host, port, certfile, keyfile, cafile):
    loop = asyncio.get_event_loop()
    loop.run_until_complete(run_server(host, port, certfile, keyfile))
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        loop.close()

if __name__ == "__main__":
    print("QUIC server")
    parser = argparse.ArgumentParser(description='Server for receiving files over QUIC')
    parser.add_argument('-host', help='Host IP address', required=True)
    parser.add_argument('-certfile', help='Path to certificate file', required=True)
    parser.add_argument('-keyfile', help='Path to key file', required=True)
    parser.add_argument('-cafile', help='Path to CA file', required=True)
    args = parser.parse_args()
    print("listening on " + str(args.host) + " 25565")
    main(args.host, 25565, args.certfile, args.keyfile, args.cafile)
