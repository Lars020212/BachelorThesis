import socket
import ssl
import datetime
import argparse
import pickle


def receive_files(server_socket):
    # Receive the number of files from the client
    received_size_total = 0
    num_files_bytes = server_socket.recv(4)
    num_files = int.from_bytes(num_files_bytes, byteorder="big")

    for _ in range(num_files):
        # Receive file name length and file name
        file_name_length_bytes = server_socket.recv(4)
        file_name_length = int.from_bytes(file_name_length_bytes, byteorder="big")
        file_name = server_socket.recv(file_name_length).decode("utf-8")

        #file_path = "/home/lurr3t/exjobb/src/bin/" + file_name

        # Receive file size
        file_size_bytes = server_socket.recv(8)
        file_size = int.from_bytes(file_size_bytes, byteorder="big")

        # Receive file data
        received_size = 0
        #with open(file_path, "wb") as file:
        while received_size < file_size:
            chunk = server_socket.recv(4096)
            received_size += len(chunk)
            #file.write(chunk)
        received_size_total += received_size
        print("received kB:" + str(received_size / 1000))
    end_time = datetime.datetime.now()
    print("received total Mb:" + str(received_size_total / 1000000))
    return end_time

def main(host, certfile, keyfile, cafile):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=certfile, keyfile=keyfile)
    context.load_verify_locations(cafile=cafile)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((host, 25565))
        s.listen(0)

        with context.wrap_socket(s, server_side=True) as server_socket:
            while True:
                connection, client_address = server_socket.accept()
                with connection:

                    try:
                        end_time = receive_files(connection)
                        print(f"End time: {end_time}")

                        # Send end time back to client
                        connection.sendall(pickle.dumps(end_time))
                        connection.close()
                        print("Files received and end time sent to client.")
                    except ConnectionResetError:
                        print("Connection closed by client before receiving end time.")
                    except Exception as e:
                        print("An error occurred:", e)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Server for receiving files over TLS')
    parser.add_argument('-host', help='Host IP address', required=True)
    parser.add_argument('-certfile', help='Path to certificate file', required=True)
    parser.add_argument('-keyfile', help='Path to key file', required=True)
    parser.add_argument('-cafile', help='Path to CA file', required=True)
    args = parser.parse_args()
    print("listening on " + str(args.host) + " 25565" )
    main(args.host, args.certfile, args.keyfile, args.cafile)
