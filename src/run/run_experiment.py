import lzma
import os
import pickle
import subprocess
import sys
import argparse
import time
import datetime
import pylzma
import statistics
import brotli
import zstandard as zstd


# Lägg till src-mappen i Python path
sys.path.append("/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main")

# Importera som src.lib.*
from Client import Client
from Server import Server
from src.lib.Utils import Utils
from src.lib.ExcelParser import ExcelParser


USE_LSQUIC = True

# OPTIONS
DYNAMIC_FILE_PATH = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/dynamic"
END_TIME_PATH = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/end_time.b"
START_TIME_PATH = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/start_time.b"
MUTEX = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/mutex.txt"
START_PORT = 49152
# The number of iterations for each file size
EPISODE_ITERATIONS = 20
MAX_FILE_SIZE_KB = 31000
MIN_FILE_SIZE_KB = 31000
INCREMENT_FILE_SIZE_KB = 1000
# empty if the complete silesia corpus should be run
RUN_FILE = "webster"
CLIENT_LOG: str = ""
SERVER_LOG: str = ""
# Ensures that the server has started before the client


def write_mutex(i):
    #print(f"writing {i} to mutex")
    while True:
        try:
            with open(MUTEX, "w") as mf:
                mf.write(str(i))
                mf.close()
                break
        except:
            print("Couldnt't save to mutex\n")
            continue



def read_mutex(i):
    flip = False
    while True:
        with open(MUTEX, "r") as mf:
            content = mf.read()
            if str(i) == content:
                mf.close()
                break
            else:
                if not flip:
                    #print(f"---CLIENT LOG---\n{CLIENT_LOG}\n\n\n")
                    #print(f"---SERVER LOG---\n{SERVER_LOG}\n")
                    print("")
                    print(f"waiting on mutex, it contains: {content}")
                    time.sleep(0.5)
                    flip = True
                pass

# Makes sure that the correct parameters are included
def arg_parser():
    parser = argparse.ArgumentParser(description='Experiment for sending files over TCP and QUIC')
    parser.add_argument('-host', help='Host IP address', required=True)
    parser.add_argument('-certfile', help='Path to certificate file', required=True)
    parser.add_argument('-keyfile', help='Path to key file', required=True)
    parser.add_argument('-cafile', help='Path to CA file', required=True)
    parser.add_argument('-c', help='run client', required=False)
    parser.add_argument('-s', help='run server', required=False)
    args = parser.parse_args()
    # flag control
    if args.s:
        pass
    elif args.c:
        pass
    else:
        print("Missing either -c or -s flag")
        exit(1)
    return args


def get_end_time_from_client_c(cl: Client, i):
    with open(START_TIME_PATH, "rb") as file:
        full_content_array = file.read().split(b"|")
        start_time_string = full_content_array[-1].decode("utf-8").strip()
        global CLIENT_LOG
        CLIENT_LOG = file.read()


        #print(f"start time: {str(start_time)}")
        start_time = datetime.datetime.strptime(start_time_string, "%Y-%m-%d %H:%M:%S.%f")
        with open(END_TIME_PATH, "rb") as bf:
            end_time_string = cl.retrieve_end_time_quic(last_iteration_before=i, end_time_path=END_TIME_PATH)
            #print(f"end time: {str(end_time_string)}")
            end_time = datetime.datetime.strptime(end_time_string, "%Y-%m-%d %H:%M:%S.%f")
            #end_time = datetime.datetime.strptime(end_time, "%Y-%m-%d %H:%M:%S.%f")
            return start_time, end_time


def run_client_episode(protocol, args, buffer, port):
    episode_total_latency = 0
    ut = Utils()
    cl = Client(args.host, args.certfile, args.keyfile, args.cafile, port, END_TIME_PATH)
    episode_run_times = []
    for i in range(EPISODE_ITERATIONS):

        start_time = datetime.datetime.now()
        end_time = 0
        # clears the compressed files for each iteration
        ut.remove_all_files("/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/compressed")
        if USE_LSQUIC:
            read_mutex(i)

        # for quic
        file_paths = ut.get_all_file_paths(DYNAMIC_FILE_PATH)
        flags = ""
        for file in file_paths:
            flags += f"-f {file} "

        if protocol == "quic":
            if not USE_LSQUIC:
                end_time = cl.start_quic(ut.buffer_to_byte_array(buffer))
            else:
                #lsquic
                client = "/home/lars/Skola/examensarbete/lsquic/bin/md5_client"
                os.system(
                    f"{client} -s {args.host}:{port} {flags}]" + " > " + START_TIME_PATH)
                start_time, end_time = get_end_time_from_client_c(cl, i)

        elif protocol == "quic_compress":
            if not USE_LSQUIC:
                end_time = cl.start_quic(pylzma.compress(buffer, algorithm=0))
            else:
                # lsquic
                client = "/home/lars/Skola/examensarbete/lsquic/bin/md5_client"
                os.system(
                    f"{client} -s {args.host}:{port} -z zlib {flags}" + " > " + START_TIME_PATH)
                start_time, end_time = get_end_time_from_client_c(cl, i)

        elif protocol == "tcp":
            if not USE_LSQUIC:
                end_time = cl.run_tcp(ut.buffer_to_byte_array(buffer))
            else:
                client = "/home/lurr3t/exjobb/tcp_client"
                os.system(
                    f"{client} -p {port} -h {args.host} -c {args.certfile} -k {args.keyfile}" + " > " + START_TIME_PATH)
                start_time, end_time = get_end_time_from_client_c(cl, i)
            #port += 1

        elif protocol == "tcp_compress":
            if not USE_LSQUIC:
                end_time = cl.run_tcp(pylzma.compress(buffer, algorithm=0))
            else:
                client = "/home/lurr3t/exjobb/tcp_client"
                os.system(
                    f"{client} -z -p {port} -h {args.host} -c {args.certfile} -k {args.keyfile}" + " > " + START_TIME_PATH)
                start_time, end_time = get_end_time_from_client_c(cl, i)
            #port += 1
        elif protocol == "quic_brotli":
            if not USE_LSQUIC:
                compressed = brotli.compress(buffer)
                end_time = cl.start_quic(compressed)
            else:
                client = "/home/lars/Skola/examensarbete/lsquic/bin/md5_client"
                os.system(
                    f"{client} -s {args.host}:{port} -z brotli {flags} > {START_TIME_PATH}")
                start_time, end_time = get_end_time_from_client_c(cl, i)

        elif protocol == "quic_zstd":
            if not USE_LSQUIC:
                cctx = zstd.ZstdCompressor()
                compressed = cctx.compress(buffer)
                end_time = cl.start_quic(compressed)
            else:
                client = "/home/lars/Skola/examensarbete/lsquic/bin/md5_client"
                os.system(
                    f"{client} -s {args.host}:{port} -z zstd {flags} > {START_TIME_PATH}")
                start_time, end_time = get_end_time_from_client_c(cl, i)
                
        port += 1
        # Calculate end-to-end latency
        delta = end_time - start_time
        latency = "{:.1f}".format(delta.total_seconds() * 1000)

        # zeroes value if it is negative
        if float(latency) < 0:
            #episode_total_latency = 0
            print(f"latency for this run is negative: ")

        episode_run_times.append(float(latency))
        episode_total_latency += float(latency)
        print(f"{i}: {latency} ms")

    # returns average and median
    return "{:.1f}".format(episode_total_latency / EPISODE_ITERATIONS), "{:.1f}".format(statistics.median(episode_run_times)), episode_run_times




def run_client(args):
    size_i = MIN_FILE_SIZE_KB

    # For storing results
    episode = [["quic", 0, 0, []], ["quic_zlib", 0, 0, []], ["quic_zstd", 0, 0, []], ["quic_brotli", 0, 0, []], 0]
    ex = ExcelParser()

    # is needed because of this error: OSError: [Errno 98] Address already in us
    port = START_PORT
    ut = Utils()
    while size_i <= MAX_FILE_SIZE_KB:
        # clears the dynamic files for each iteration
        ut.remove_all_files("/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/dynamic")
        # create test files
        #ut.create_file(size_i * 1000, "/home/lurr3t/exjobb/payloads/dynamic")
        if not RUN_FILE:
            ut.create_dynamic_file(size_i * 1000, "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/silesia", "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/dynamic")
        else:
            ut.create_dynamic_file(size_i * 1000, "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/silesia",
                                   "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/dynamic", RUN_FILE)
        # load data buffer | will implement compression
        if not USE_LSQUIC:
            buffer = ut.get_data_buffer(DYNAMIC_FILE_PATH)
        else:
            buffer = []
        #buffer = ut.get_data_buffer(DYNAMIC_FILE_PATH)
        print(f"-----QUIC: {size_i} kB-----")
        average, median, episode_run = run_client_episode("quic", args, buffer, port)
        episode[0] = ["quic", average, median, episode_run]
        print(f"[{average} ms - Average] \n[{median} ms - Median]")
        print("--------------------")
        port += 1
        print(f"-----QUIC_zlib: {size_i} kB-----")
        average, median, episode_run = run_client_episode("quic_compress", args, buffer, port)
        episode[1] = ["quic_zlib", average, median, episode_run]
        print(f"[{average} ms - Average] \n[{median} ms - Median]")
        print("--------------------")
        port += 1
        print(f"-----QUIC_brotli: {size_i} kB-----")
        average, median, episode_run = run_client_episode("quic_brotli", args, buffer, port)
        episode[2] = ["quic_brotli", average, median, episode_run]
        print(f"[{average} ms - Average] \n[{median} ms - Median]")
        port += 1

        print(f"-----QUIC_zstd: {size_i} kB-----")
        average, median, episode_run = run_client_episode("quic_zstd", args, buffer, port)
        episode[3] = ["quic_zstd", average, median, episode_run]
        print(f"[{average} ms - Average] \n[{median} ms - Median]")
        port += 1

        #print(f"-----TCP: {size_i} kB-----")
        #average, median, episode_run = run_client_episode("tcp", args, buffer, port)
        #episode[2] = ["tcp", average, median, episode_run]
        #print(f"[{average} ms - Average] \n[{median} ms - Median]")
        #print("--------------------")
        #port += 1
        #print(f"-----TCP_compress: {size_i} kB-----")
        #average, median, episode_run = run_client_episode("tcp_compress", args, buffer, port)
        #episode[3] = ["tcp_compress", average, median, episode_run]
        #print(f"[{average} ms - Average] \n[{median} ms - Median]")
        #print("--------------------")
        #port += 1


        # add to excel
        episode[4] = size_i
        ex.load_episode(episode)

        #port += 1
        size_i += INCREMENT_FILE_SIZE_KB
    #ut.clear_file(DYNAMIC_FILE_PATH)





# Makes sure to have the server online until it has received as many files as the EPISODE_ITERATIONS
def run_server_episode(protocol, args, port, compress):
    print(f"Server start episode {protocol}")
    se = Server(args.host, args.certfile, args.keyfile, args.cafile, EPISODE_ITERATIONS, port, compress, END_TIME_PATH)

    if protocol == "quic":
        print("Waiting on QUIC connection")
        se.start_quic()
    elif protocol == "quic_compress":
        print("Waiting on QUIC compress connection")
        se.start_quic()
    elif protocol == "tcp":
        print("Waiting on TCP connection")
        se.run_tcp()
    elif protocol == "tcp_compress":
        print("Waiting on TCP compress connection")
        se.run_tcp()

    print(f"Server end episode {protocol}")

def run_server(args):
    # Number of runs
    runs = MAX_FILE_SIZE_KB // INCREMENT_FILE_SIZE_KB
    # is needed because of this error: OSError: [Errno 98] Address already in us
    port = START_PORT

    # AIOQUIC
    if not USE_LSQUIC:
        for i in range(runs):
            run_server_episode("quic", args, port, False)
            port += 1
            run_server_episode("quic_compress", args, port, True)
            run_server_episode("tcp", args, port, False)
            run_server_episode("tcp_compress", args, port, True)
            port += 1
    # LSQUIC
    else:
        for i in range(runs):
            run_c_server_episode("quic", args, port)
            port += 1
            run_c_server_episode("quic_compress", args, port)
            port += 1
            run_c_server_episode("quic_brotli", args, port)
            port += 1
            run_c_server_episode("quic_zstd", args, port)
            port += 1
            """ run_c_server_episode("tcp", args, port)
            port += 1
            run_c_server_episode("tcp_compress", args, port)
            port += 1 """


def run_c_server_episode(protocol, args, port):
    for j in range(EPISODE_ITERATIONS):
        #run lsquic
        # /home/lurr3t/exjobb/lsquic/bin/md5_server -s localhost:49152 -c localhost,/home/lurr3t/exjobb/cert/local/server/local-server.pem,/home/lurr3t/exjobb/cert/local/server/local-server-key.pem -A Cubic
        write_mutex(j)
        intermediate = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/server_intermediate.txt"
        if protocol == "quic":
            server = "/home/lars/Skola/examensarbete/lsquic/bin/md5_server"
            os.system(
            f"{server} -s {args.host}:{port} -c {args.host},{args.certfile},{args.keyfile} -A Cubic" + " > " + intermediate)
        elif protocol == "quic_compress":
            server = "/home/lars/Skola/examensarbete/lsquic/bin/md5_server"
            os.system(
                f"{server} -s {args.host}:{port} -c {args.host},{args.certfile},{args.keyfile} -A Cubic -z zlib" + " > " + intermediate)
            
        elif protocol == "quic_brotli":
            server = "/home/lars/Skola/examensarbete/lsquic/bin/md5_server"
            os.system(
                f"{server} -s {args.host}:{port} -c {args.host},{args.certfile},{args.keyfile} -A Cubic -z brotli" + " > " + intermediate)
        elif protocol == "quic_zstd":
            server = "/home/lars/Skola/examensarbete/lsquic/bin/md5_server"
            os.system(
                f"{server} -s {args.host}:{port} -c {args.host},{args.certfile},{args.keyfile} -A Cubic -z zstd" + " > " + intermediate)
        
        """ elif protocol == "tcp":
            server = "/home/lurr3t/exjobb/tcp_server"
            os.system(
                f"{server} -p {port} -h {args.host} -c {args.certfile} -k {args.keyfile}" + " > " + intermediate)
            #port += 1
        elif protocol == "tcp_compress":
            server = "/home/lurr3t/exjobb/tcp_server"
            os.system(
                f"{server} -z -p {port} -h {args.host} -c {args.certfile} -k {args.keyfile}" + " > " + intermediate)
            #port += 1 """
        port += 1

        # Read the contents of the output file
        end_time = ""
        with open(intermediate, "rb") as file:
            full_content_array = file.read().split(b"|")
            end_time = full_content_array[-1].decode("utf-8").strip()
            global SERVER_LOG
            SERVER_LOG = file.read()
        while True:
            try:
                with open(END_TIME_PATH, "wb") as bf:
                    # Stores both the current iteration and the end time
                    print(f"saving end time: {end_time}")
                    payload = [0, end_time]
                    bf.write(pickle.dumps(payload))
                    bf.close()
                    break
            except:
                continue


if __name__ == "__main__":
    args = arg_parser()

    if (args.c):
        print("Run client")
        run_client(args)
    if (args.s):
        print("Run server")
        run_server(args)
    else:
        pass
    exit(0)