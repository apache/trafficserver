#!/usr/bin/env python3

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

import logging
import argparse
import socket
import sys
import struct
import ssl

VERSION_2_SIGNATURE = b'\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A'


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server_address", help="The server IP address to connect to.")
    parser.add_argument("server_port", type=int, help="The server port to connect to.")
    parser.add_argument("sni", help="The SNI to include in the CLIENT_HELLO.")
    parser.add_argument("proxy_src_ip", help="The source IP address in the PROXY message.")
    parser.add_argument("proxy_dest_ip", help="The destination IP address in the PROXY message.")
    parser.add_argument("proxy_src_port", type=int, help="The source port in the PROXY message.")
    parser.add_argument("proxy_dest_port", type=int, help="The destination port in the PROXY message.")
    parser.add_argument("protocol_version", type=int, choices=[1, 2], help="the proxy protocol version(either 1 or 2).")
    parser.add_argument("--https", action="store_true", help="Send https data after the PROXY message.")
    return parser.parse_args()


def construct_proxy_header_v1(src_addr: tuple, dst_addr: tuple) -> bytes:
    """Construct a PROXY protocol v1 header.

    :param src_addr: The source address and port.
    :param dst_addr: The destination address.

    :return: A PROXY protocol v1 header.
    """
    return f"PROXY TCP4 {src_addr[0]} {dst_addr[0]} {src_addr[1]} {dst_addr[1]}\r\n".encode()


def construct_proxy_header_v2(src_addr: tuple, dst_addr: tuple) -> bytes:
    """Construct a PROXY protocol v2 header.

    :param src_addr: The source address.
    :param dst_addr: The destination address.

    :return: A PROXY protocol v2 header.
    """
    header = VERSION_2_SIGNATURE
    # Protocol version 2 + PROXY command
    header += b'\x21'
    # TCP over IPv4
    header += b'\x11'
    # address length
    header += b'\x00\x0C'
    header += socket.inet_pton(socket.AF_INET, src_addr[0])
    header += socket.inet_pton(socket.AF_INET, dst_addr[0])
    header += struct.pack('!H', src_addr[1])
    header += struct.pack('!H', dst_addr[1])
    return header


def send_proxy_header(
        socket: socket.socket, src_ip: str, src_port: str, dest_ip: int, dest_port: int, proxy_protocol_version: int) -> None:
    """Send the specified PROXY protocol header.

    :param socket: The socket to send the header on.
    :param src_ip: The source IP address.
    :param src_port: The source port.
    :param dest_ip: The destination IP address.
    :param dest_port: The destination port.
    :param proxy_protocol_version: The PROXY protocol version.
    """
    logging.info(f'Sending PROXY protocol version {proxy_protocol_version}')
    if proxy_protocol_version == 1:
        header = construct_proxy_header_v1((src_ip, src_port), (dest_ip, dest_port))
    elif proxy_protocol_version == 2:
        header = construct_proxy_header_v2((src_ip, src_port), (dest_ip, dest_port))
    else:
        raise ValueError(f'Invalid proxy protocol version: {proxy_protocol_version}')

    socket.sendall(header)


def send_and_receive_http(sock: socket.socket, host: str) -> None:
    """Send and receive HTTP data on the specified socket.

    :param sock: The socket to send and receive data on.
    :param host: The Host header value to send in the HTTP request.
    """
    request = f"GET /proxy_protocol HTTP/1.1\r\nHost: {host}\r\n\r\n"
    logging.info("Sending:")
    logging.info(f'\n{request}')
    sock.sendall(request.encode())

    response = b''
    while True:
        data = sock.recv(1024)
        if not data:
            break
        response += data
    logging.info("Received:")
    logging.info(f'\n{response.decode()}')


def main() -> None:
    """Open and HTTP(S) connection with a Proxy Protocol header."""
    args = parse_args()
    with socket.create_connection((args.server_address, args.server_port)) as sock:
        # send the PROXY header
        send_proxy_header(
            sock, args.proxy_src_ip, args.proxy_src_port, args.proxy_dest_ip, args.proxy_dest_port, args.protocol_version)
        if args.https:
            # https
            context = ssl.create_default_context()
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            with context.wrap_socket(sock, server_hostname=args.sni) as ssock:
                send_and_receive_http(ssock, args.sni)
        else:
            # plain http
            send_and_receive_http(sock, args.sni)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    sys.exit(main())
