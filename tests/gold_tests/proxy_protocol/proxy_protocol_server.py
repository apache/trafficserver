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

"""A simple server that expects and prints out the Proxy Protocol string."""

import argparse
import logging
import socket
import ssl
import struct
import sys
import threading


# Set a 10ms timeout for socket operations.
TIMEOUT = .010

PP_V2_PREFIX = b'\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a'


# Create a condition variable for thread initialization.
internal_thread_is_ready = threading.Condition()


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "certfile",
        help="The path to the certificate file to use for TLS.")
    parser.add_argument(
        "keyfile",
        help="The path to the key file to use for TLS.")
    parser.add_argument(
        "address",
        help="The IP address to listen on.")
    parser.add_argument(
        "port",
        type=int,
        help="The port to listen on.")
    parser.add_argument(
        "internal_port",
        type=int,
        help="The internal port used to parse the TLS content.")
    parser.add_argument(
        "--plaintext",
        action="store_true",
        help="Listen for plaintext connections instead of TLS.")

    return parser.parse_args()


def receive_and_send_http(sock: socket.socket) -> None:
    """Receive and send an HTTP request and response.

    :param sock: The socket to receive the request on.
    """
    sock.settimeout(TIMEOUT)

    # Read the request until the final CRLF is received.
    received_request = b''
    while True:
        data = None
        try:
            data = sock.recv(1024)
            logging.debug(f'Internal: received {len(data)} bytes')
        except socket.timeout:
            continue
        if not data:
            break
        received_request += data

        if b'\r\n\r\n' in received_request:
            break
    logging.info("Received request:")
    logging.info(received_request.decode("utf-8"))

    # Send a response.
    response = (
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    )
    logging.info(f'Sending:\n{response}')
    try:
        sock.sendall(response.encode("utf-8"))
    except socket.timeout:
        logging.error("Timeout sending a response.")


def run_internal_server(cert_file: str, key_file: str,
                        address: str, port: int,
                        plaintext: bool) -> None:
    """Run the internal server.

    This server is receives the HTTP content with the Proxy Protocol prefix
    stripped off by the client.

    :param cert_file: The path to the certificate file to use for TLS.
    :param key_file: The path to the key file to use for TLS.
    :param address: The IP address to listen on.
    :param port: The port to listen on.
    :param plaintext: Whether to listen for HTTP rather than HTTPS traffic.
    """

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((address, port))
        sock.listen()
        logging.info(f"Internal HTTPS server listening on {address}:{port}")

        if plaintext:
            # Notify the waiting thread that the internal server is ready.
            with internal_thread_is_ready:
                internal_thread_is_ready.notify()
            conn, addr_in = sock.accept()
            logging.info(f"Internal server accepted plaintext connection from {addr_in}")
            with conn:
                receive_and_send_http(conn)
        else:
            # Wrap the server socket to handle TLS.
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certfile=cert_file, keyfile=key_file)

            with context.wrap_socket(sock, server_side=True) as ssock:
                with internal_thread_is_ready:
                    internal_thread_is_ready.notify()
                conn, addr_in = ssock.accept()
                logging.info(f"Internal server accepted TLS connection from {addr_in}")
                with conn:
                    receive_and_send_http(conn)


def parse_pp_v1(pp_bytes: bytes) -> int:
    """Parse and print the Proxy Protocol v1 string.

    :param pp_bytes: The bytes containing the Proxy Protocol string. There may
    be more bytes than the Proxy Protocol string.

    :returns: The number of bytes occupied by the proxy v1 protocol.
    """
    # Proxy Protocol v1 string ends with CRLF.
    end = pp_bytes.find(b'\r\n')
    if end == -1:
        raise ValueError("Proxy Protocol v1 string ending not found")
    logging.info(pp_bytes[:end].decode("utf-8"))
    return end + 2


def parse_pp_v2(pp_bytes: bytes) -> int:
    """Parse and print the Proxy Protocol v2 string.

    :param pp_bytes: The bytes containing the Proxy Protocol string. There may
    be more bytes than the Proxy Protocol string.

    :returns: The number of bytes occupied by the proxy v2 protocol string.
    """

    # Skip the 12 byte header.
    pp_bytes = pp_bytes[12:]
    version_command = pp_bytes[0]
    pp_bytes = pp_bytes[1:]
    family_protocol = pp_bytes[0]
    pp_bytes = pp_bytes[1:]
    tuple_length = int.from_bytes(pp_bytes[:2], byteorder='big')
    pp_bytes = pp_bytes[2:]

    # Of version_command, the highest 4 bits is the version and the lowest is
    # the command.
    version = version_command >> 4
    command = version_command & 0x0F

    if version != 2:
        raise ValueError(
            f'Invalid version: {version} (by spec, should always be 0x02)')

    if command == 0x0:
        command_description = 'LOCAL'
    elif command == 0x1:
        command_description = 'PROXY'
    else:
        raise ValueError(
            f'Invalid command: {command} (by spec, should be 0x00 or 0x01)')

    # Of address_family, the highest 4 bits is the address family and the
    # lowest is the transport protocol.
    if family_protocol == 0x0:
        transport_protocol_description = 'UNSPEC'
    elif family_protocol == 0x11:
        transport_protocol_description = 'TCP4'
    elif family_protocol == 0x12:
        transport_protocol_description = 'UDP4'
    elif family_protocol == 0x21:
        transport_protocol_description = 'TCP6'
    elif family_protocol == 0x22:
        transport_protocol_description = 'UDP6'
    elif family_protocol == 0x31:
        transport_protocol_description = 'UNIX_STREAM'
    elif family_protocol == 0x32:
        transport_protocol_description = 'UNIX_DGRAM'
    else:
        raise ValueError(
            f'Invalid address family: {family_protocol} (by spec, should be '
            '0x00, 0x11, 0x12, 0x21, 0x22, 0x31, or 0x32)')

    if family_protocol in (0x11, 0x12):
        if tuple_length != 12:
            raise ValueError(
                "Unexpected tuple length for TCP4/UDP4: "
                f"{tuple_length} (by spec, should be 12)"
            )
        src_addr = socket.inet_ntop(socket.AF_INET, pp_bytes[:4])
        pp_bytes = pp_bytes[4:]
        dst_addr = socket.inet_ntop(socket.AF_INET, pp_bytes[:4])
        pp_bytes = pp_bytes[4:]
        src_port = int.from_bytes(pp_bytes[:2], byteorder='big')
        pp_bytes = pp_bytes[2:]
        dst_port = int.from_bytes(pp_bytes[:2], byteorder='big')
        pp_bytes = pp_bytes[2:]

    tuple_description = f'{src_addr} {dst_addr} {src_port} {dst_port}'
    logging.info(
        f'{command_description} {transport_protocol_description} '
        f'{tuple_description}')

    return 16 + tuple_length


def accept_pp_connection(sock: socket.socket, address: str, internal_port: int) -> bool:
    """Accept a connection and parse the proxy protocol header.

    :param sock: The socket to accept the connection on.
    :param address: The address of the internal server to connect to.
    :param internal_port: The port of the internal server to connect to.

    :returns: True if the connection had a payload, False otherwise.
    """
    client_conn, address_in = sock.accept()
    logging.info(f'Accepted connection from {address_in}')
    with client_conn:
        has_pp = False
        pp_length = 0
        # Read the Proxy Protocol prefix, which ends with the first CRLF.
        received_data = b''
        while True:
            data = client_conn.recv(1024)
            if data:
                logging.debug(f"Received: {len(data)} bytes")
            else:
                logging.info("No data received while waiting for "
                             "Proxy Protocol prefix")
                return False
            received_data += data

            if (received_data.startswith(b'PROXY') and
                    b'\r\n' in received_data):
                logging.info("Received Proxy Protocol v1")
                pp_length = parse_pp_v1(received_data)
                has_pp = True
                break

            if received_data.startswith(PP_V2_PREFIX):
                logging.info("Received Proxy Protocol v2")
                pp_length = parse_pp_v2(received_data)
                has_pp = True
                break

            if len(received_data) > 108:
                # The spec guarantees that the prefix will be no more than
                # 108 bytes.
                logging.info("No Proxy Protocol string found.")
                break
        if has_pp:
            # Now, strip the received_data of the prefix and blind tunnel
            # the rest of the content.
            for_internal = received_data[pp_length:]
            logging.debug(
                f"Stripped the prefix, now there are {len(for_internal)} "
                "bytes for the internal server.")
        else:
            for_internal = received_data
        client_conn.settimeout(TIMEOUT)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as internal_sock:
            logging.debug(f"Connecting to internal server on {address}:{internal_port}")
            internal_sock.connect((address, internal_port))
            internal_sock.settimeout(TIMEOUT)
            if for_internal:
                logging.debug('Sending remaining data to internal server: '
                              f'{len(for_internal)} bytes')
                internal_sock.sendall(for_internal)
            while True:

                logging.debug("entering loop")

                try:
                    from_internal = internal_sock.recv(1024)
                    logging.debug(f'Received {len(from_internal)} bytes from internal server')
                    if not from_internal:
                        logging.debug('No more data from internal server, closing connection')
                        break
                    client_conn.sendall(from_internal)
                    logging.debug(f'Sent {len(from_internal)} bytes to client')
                except socket.timeout:
                    pass

                try:
                    for_internal = client_conn.recv(1024)
                    logging.debug(f'Received {len(for_internal)} bytes from client')
                    if not for_internal:
                        logging.debug('No more data from client, closing connection')
                        break
                    internal_sock.sendall(for_internal)
                    logging.debug(f'Sent {len(for_internal)} bytes to internal server')
                except socket.timeout:
                    pass


def receive_pp_request(address: str, port: int, internal_port: int) -> None:
    """Start a server to receive a connection which may have a proxy protocol
    header.

    :param address: The address to listen on.
    :param port: The port to listen on.
    :param internal_port: The port of the internal server to connect to.
    """

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((address, port))
        sock.listen()

        # The PortOpen logic will create an empty request to the server. Ignore
        # those until we have a connection with a real request which comes in.
        request_received = False
        while not request_received:
            request_received = accept_pp_connection(sock, address,
                                                    internal_port)


def main() -> int:
    """Run the server listening for Proxy Protocol."""
    args = parse_args()

    with internal_thread_is_ready:
        """Start the threads to receive requests."""
        internal_server = threading.Thread(
            target=run_internal_server,
            args=(args.certfile, args.keyfile, args.address,
                  args.internal_port, args.plaintext))
        internal_server.start()

        # Wait for the internal server to start before proceeding.
        internal_thread_is_ready.wait()

    receive_pp_request(args.address, args.port, args.internal_port)
    internal_server.join()

    return 0


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    sys.exit(main())
