'''
A simple forwarding proxy that forwards all traffic from one local port to
another, while keeping track of the number of bytes transferred in each
direction.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import socket
import threading
import argparse

LOCAL_HOST = '127.0.0.1'
TIMEOUT = 0.5
# Create a thread-local data object to store the number of bytes transferred.
thread_local_data = threading.local()


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--listening_port',
        type=int,
        help='Port where the proxy listens.')

    parser.add_argument(
        '--forwarding_port',
        type=int,
        help='Server\'s port to forward to.')

    return parser.parse_args()


def initialize_thread_local_data():
    thread_local_data.client_to_server_bytes = 0
    thread_local_data.server_to_client_bytes = 0


def forward(source, destination, is_client_to_server):
    """Forward traffic from source to destination.

    :param source: socket to read from.
    :param destination: socket to write to.
    :param is_client_to_server: True if forwarding from client to server.
    """
    # Initialize thread-local data.
    initialize_thread_local_data()

    while True:
        try:
            data = source.recv(4096)
            if not data:
                break
            destination.sendall(data)
        except Exception as e:
            # Catching all exceptions.
            break
        if is_client_to_server:
            thread_local_data.client_to_server_bytes += len(data)
        else:
            thread_local_data.server_to_client_bytes += len(data)
    # Forwarding done. Print the number of bytes transferred in the direction.
    print_transmitted_bytes_if_any(is_client_to_server)


def print_transmitted_bytes_if_any(is_client_to_server):
    """Print the number of bytes transferred in the direction if any.

    :param is_client_to_server: True if forwarding from client to server.
    """
    if is_client_to_server:
        if thread_local_data.client_to_server_bytes > 0:
            print(f"client-to-server: {thread_local_data.client_to_server_bytes}")
    else:
        if thread_local_data.server_to_client_bytes > 0:
            print(f"server-to-client: {thread_local_data.server_to_client_bytes}")


def start_bidirectional_forwarding(client_socket, forwarding_port):
    """Start forwarding traffic between client and server.

    :param client_socket: socket connected to the client.
    :param forwarding_port: server port to forward to.
    """
    CLIENT_TO_SERVER = True
    SERVER_TO_CLIENT = False
    with client_socket, socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        client_socket.settimeout(TIMEOUT)
        server_socket.settimeout(TIMEOUT)
        server_socket.connect((LOCAL_HOST, forwarding_port))
        # Spawn a thread to forward traffic from client to server.
        client_to_server = threading.Thread(target=forward, args=(client_socket, server_socket, CLIENT_TO_SERVER))
        client_to_server.start()

        # Forward traffic from server to client in the current thread.
        forward(server_socket, client_socket, SERVER_TO_CLIENT)
        client_to_server.join()


def main() -> int:
    """Run the proxy."""
    print(f"Starting proxy...")
    args = parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listen_socket:
        listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listen_socket.bind((LOCAL_HOST, args.listening_port))
        listen_socket.listen()
        print(f"Proxy listening on {LOCAL_HOST}:{args.listening_port}")
        try:
            while True:
                client_sock, client_addr = listen_socket.accept()
                print(f"Accepted connection from {client_addr}")
                # Handle each client connection in a new thread.
                client_thread = threading.Thread(target=start_bidirectional_forwarding, args=(client_sock, args.forwarding_port))
                client_thread.start()
        except Exception:
            # Catching all exceptions.
            pass
        except KeyboardInterrupt:
            print("Caught KeyboardInterrupt, terminating the program")
            return 0


if __name__ == "__main__":
    main()
