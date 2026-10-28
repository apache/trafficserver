#!/usr/bin/env python3
'''Send a pre-recorded TLS CLIENT_HELLO as a configurable number
of packets to a server.'''
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

import argparse
import base64
import socket
import sys
import time

# This is a ClientHello packet captured from a real client using Wireshark.
# The packet includes key material necessary for the Kyber cipher handshake.
#
# NOTE: if this changes, update EXPECTED_CLIENT_HELLO_LENGTH in the
# corresponding receive_split_client_hello.py script.
client_hello_b64 = \
"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"

client_hello = base64.b64decode(client_hello_b64, validate=True)


def send_clienthello(dest_host: str, dest_port: int, split_size: int, num_data_packets: int) -> int:
    '''Send a pre-recorded TLS CLIENT_HELLO to a server.
    :param dest_host: The destination host to which to send the CLIENT_HELLO.
    :param dest_port: The destination port to which to send the CLIENT_HELLO.
    :param split_size: The size of each split packet. 0 means no split,
        send the whole packet at once.
    :param num_data_packets: The number of data packets to send.
    :return: 0 on success, non-zero on failure.
    '''
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setblocking(True)
        timeout = 3
        s.settimeout(timeout)
        print(f'Connecting to: {dest_host}:{dest_port}')
        try:
            s.connect((dest_host, dest_port))
        except socket.error as e:
            print(f"Failed to connect to {dest_host}:{dest_port} - {e}")
            return 1
        print('Connection successful.')
        split_size = split_size if split_size > 0 else len(client_hello)
        client_hello_packets = [client_hello[i:i+split_size] \
                                for i in range(0, len(client_hello), split_size)]
        print(
            f'Sending ClientHello of {len(client_hello)} bytes in {split_size} '
            f'byte packets, {len(client_hello_packets)} total packets.')
        for packet in client_hello_packets:
            print(f'Sending packet of size {len(packet)} bytes.')
            s.send(packet)
            time.sleep(0.05)
        print('CLIENT_HELLO sent, waiting for response.')
        try:
            buff = s.recv(10240)
        except socket.timeout:
            print(f"Failed: no response received within {timeout} seconds.")
            return 1
        except socket.error as e:
            print(f"Failed to receive response - {e}")
            return 1
        except Exception as e:
            print(f"An unexpected error occurred while receiving response - {e}")
            return 1
        print("Response received:")
        print(buff.decode('utf-8', errors='ignore'))

        print(f'Sending {num_data_packets} dummy data packets.')
        for i in range(num_data_packets):
            time.sleep(0.05)
            print(f'Sending dummy data packet {i} of size {len(buff)} bytes.')
            send_buf = f'data: {i}\n'.encode('utf-8')
            try:
                s.send(send_buf)
            except socket.error as e:
                print(f"Failed to send dummy data packet {i} - {e}")
                return 1
            # Wait for a response after each data packet
            try:
                response_buff = s.recv(10240)
            except socket.timeout:
                print(f"Failed: no response received after data packet {i}.")
                return 1
            except socket.error as e:
                print(f"Failed to receive response after data packet {i} - {e}")
                return 1
            except Exception as e:
                print(f"An unexpected error occurred while receiving response after data packet {i} - {e}")
                return 1
            print(f"Response after data packet {i} received:")
            print(response_buff.decode('utf-8', errors='ignore'))
            # Verify that the response was an echo of the request.
            if response_buff != send_buf:
                print(f"Response after data packet {i} did not match sent data: {response_buff}")
                return 1
            else:
                print(f"Response after data packet {i} matched sent data.")
        print("All packets sent successfully.")
        return 0


def parse_args() -> argparse.Namespace:
    '''Parse command line arguments.
    :return: The parsed arguments.
    '''
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dest_host", help="Destination host to which to send the CLIENT_HELLO.")
    parser.add_argument('dest_port', type=int, help="Destination port to which to send the CLIENT_HELLO.")
    parser.add_argument(
        '--split_size',
        '-s',
        type=int,
        default=1100,
        help="Size of each split packet. Default is 1100 bytes. "
        "0 means no split, send the whole packet at once.")
    parser.add_argument('--num_data_packets', '-d', type=int, default=2, help="Number of data packets to send. Default is 2.")
    return parser.parse_args()


def main() -> int:
    '''Send the CLIENT_HELLO split into two packets.
    :return: 0 on success, non-zero on failure.
    '''
    args = parse_args()
    status = send_clienthello(args.dest_host, args.dest_port, args.split_size, args.num_data_packets)
    if status == 0:
        print("CLIENT_HELLO sent successfully.")
    else:
        print("CLIENT_HELLO failed.")


if __name__ == "__main__":
    sys.exit(main())
