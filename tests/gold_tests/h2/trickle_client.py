'''Implement a client that sends many small DATA frames.'''

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
from email.message import EmailMessage as HttpHeaders
import logging
import math
import socket
import sys
import ssl
from OpenSSL.SSL import Error as SSLError
from OpenSSL.SSL import SysCallError as SSLSysCallError
import statistics
import traceback
import time

from typing import Dict, List, Tuple

import eventlet
from h2.connection import H2Connection
from h2.events import StreamEnded, ResponseReceived, DataReceived, TrailersReceived, StreamReset, ConnectionTerminated
from h2.exceptions import StreamClosedError


def get_body_text() -> bytes:
    """Create a body of text to send in the request."""
    body_items: List[str] = []
    for i in range(100):
        # Create a chunk of 0 padded bytes, followed by a space.
        chunk_payload = f'{i:06x} '.encode("utf-8")
        body_items.append(chunk_payload)
    return b''.join(body_items)


class RequestInfo:
    """POD for request headers, etc."""

    def __init__(self, stream_id: int, headers: Dict[str, str], body: str):
        self.stream_id: int = stream_id
        self.headers: Dict[str, str] = headers
        self.body_bytes: str = body


class ResponseInfo:
    """POD for response headers, etc."""

    def __init__(
            self,
            status: int,
            headers: Dict[bytes, bytes],
            body: bytes,
            trailers: Dict[bytes, bytes] = None,
            errors: List[str] = None):
        self.status_code: int = status
        self.headers: Dict[bytes, bytes] = headers
        self.body_bytes: bytes = body
        self.trailers: Dict[bytes, bytes] = trailers
        self.errors: List[str] = errors


def print_transaction(request: RequestInfo, response: ResponseInfo) -> None:
    """Print a description of the transaction.

    :param request: The details about the request.
    :param response: The details about the response.
    """

    description = "\n==== REQUEST HEADERS ====\n"
    for k, v in request.headers.items():
        if isinstance(k, bytes):
            k, v = (k.decode('ascii'), v.decode('ascii'))
        description += f"{k}: {v}\n"

    if request.body_bytes is not None:
        description += f"\n==== REQUEST BODY ====\n{request.body_bytes}\n"

    description += "\n==== RESPONSE ====\n"
    description += f"{response.status_code}\n"

    description += "\n==== RESPONSE HEADERS ====\n"
    for k, v in response.headers:
        if isinstance(k, bytes):
            k, v = (k.decode('ascii'), v.decode('ascii'))
        description += f"{k}: {v}\n"

    if response.body_bytes is not None:
        description += f"\n==== RESPONSE BODY ====\n{response.body_bytes.decode()}\n"

    if response.trailers is not None:
        description += "\n==== RESPONSE TRAILERS ====\n"
        for k, v in response.trailers.items():
            if isinstance(k, bytes):
                k, v = (k.decode('ascii'), v.decode('ascii'))
            description += "{k}: {v}\n"

    description += "\n==== END ====\n"

    logging.info(description)


class Http2Connection:
    '''
    This class manages a single HTTP/2 connection to a server. It is not
    thread-safe. For our purpose though, no lock is neccessary as the streams of
    each connection are processed sequentially.
    '''

    def __init__(self, sock, h2conn):
        self.sock = sock
        self.conn = h2conn

    def send_request(self, request: RequestInfo) -> Tuple[ResponseInfo, List[int]]:
        '''
        Sends a request to the h2 connection and returns the response object containing the headers, body, and possible errors.
        '''
        self.conn.send_headers(request.stream_id, request.headers.items())
        logging.info(f'Sent headers.')
        # Send the data over the socket.
        self.sock.sendall(self.conn.data_to_send())
        response_headers_raw = None
        response_body = b''
        response_stream_ended = False
        request_stream_ended = False
        trailers = None
        errors = []
        bytes_sent = 0
        bytes_left = len(request.body_bytes)
        data_frame_differentials: List[int] = []
        time_of_last_frame = time.perf_counter_ns()
        while not response_stream_ended:

            send_window = self.conn.local_flow_control_window(request.stream_id)
            bytes_to_send = min(send_window, bytes_left)
            # Send one byte at a time, every millisecond.
            while bytes_to_send > 0:
                chunk_size = 1
                byte_to_send = request.body_bytes[bytes_sent:bytes_sent + chunk_size]
                logging.debug(f'Sending {byte_to_send}')
                self.conn.send_data(request.stream_id, byte_to_send)
                self.sock.sendall(self.conn.data_to_send())
                bytes_left -= chunk_size
                bytes_sent += chunk_size
                bytes_to_send -= chunk_size
                time.sleep(0.001)

            if not request_stream_ended and bytes_left == 0:
                logging.debug('Closing the connection')
                self.conn.end_stream(request.stream_id)
                request_stream_ended = True

            logging.debug('Reading any responses from the socket')
            data = self.sock.recv(65536 * 1024)
            if not data:
                break

            # Feed raw data into h2 engine, and process resulting events.
            logging.debug('Feeding the data into the connection')
            events = self.conn.receive_data(data)
            have_counted_data_delay = False
            for event in events:
                if isinstance(event, ResponseReceived):
                    # Received response headers.
                    response_headers_raw = event.headers
                    time_of_last_frame = time.perf_counter_ns()
                    logging.info('Received response headers.')
                if isinstance(event, DataReceived):
                    # Update flow control so the server doesn't starve us.
                    self.conn.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    # Received more response body data.
                    response_body += event.data
                    current_time = time.perf_counter_ns()
                    ms_delay = (current_time - time_of_last_frame) / (1000 * 1000)
                    if not have_counted_data_delay:
                        data_frame_differentials.append(ms_delay)
                        time_of_last_frame = time.perf_counter_ns()
                        logging.debug(f"Received {len(event.data)} bytes of data after {ms_delay} ms")
                        have_counted_data_delay = True
                if isinstance(event, TrailersReceived):
                    # Received trailer headers.
                    trailers = event.headers
                if isinstance(event, StreamReset):
                    # Stream reset by the server.
                    logging.debug(f"Received RST_STREAM from the server: {event}")
                    errors.append('StreamReset')
                    response_stream_ended = True
                    break
                if isinstance(event, ConnectionTerminated):
                    # Received GOAWAY frame from the server.
                    logging.debug(f"Received GOAWAY from the server: {event}")
                    errors.append('ConnectionTerminated')
                    response_stream_ended = True
                    break
                if isinstance(event, StreamEnded):
                    # Received complete response body.
                    logging.info('Received stream end.')
                    response_stream_ended = True
                    break

            if not errors:
                # Send any pending data to the server.
                self.sock.sendall(self.conn.data_to_send())

        # Decode the header fields.
        status_code = next((t[1] for t in response_headers_raw if t[0].decode() == ':status'), None)
        status_code = int(status_code) if status_code else 0
        return ResponseInfo(status_code, response_headers_raw, response_body, trailers, errors), data_frame_differentials

    def close(self):
        """Tell the server we are closing the h2 connection."""
        self.conn.close_connection()
        self.sock.sendall(self.conn.data_to_send())
        self.sock.close()


def create_ssl_context(cert):
    """
    Create a SSL context with the given cert file.
    """
    ctx = ssl.create_default_context()
    ctx.set_alpn_protocols(['h2', 'http/1.1'])
    # Load the cert file
    ctx.load_cert_chain(cert)
    # Do not verify the server's certificate
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


def send_http2_request_to_server(hostname: str, port: int, cert_file: str, write_timeout: int) -> int:
    """Establish a connection with the server and send a request.

    :param hostname: The hostname to use for the :authority header.
    :param port: The port to connect to.
    :param cert_file: The TLS certificate file.
    :param write_timeout: The expected maximum amount of time frames should be delivered.

    :return: 0 if the request was successful, 1 otherwise.
    """

    request_headers = HttpHeaders()
    request_headers.add_header(':method', 'GET')
    request_headers.add_header(':path', '/some/path')
    request_headers.add_header(':authority', hostname)
    request_headers.add_header(':scheme', 'https')

    scheme = request_headers[':scheme']
    replay_server = f"127.0.0.1:{port}"
    path = request_headers[':path']
    authority = request_headers.get(':authority', '')

    stream_id = 1
    body = get_body_text()
    request: RequestInfo = RequestInfo(stream_id, request_headers, body)

    try:
        # Open a socket to the server and initiate TLS/SSL.
        ssl_context = create_ssl_context(cert=cert_file)
        setattr(ssl_context, "old_wrap_socket", ssl_context.wrap_socket)

        def new_wrap_socket(sock, *args, **kwargs):
            # Make the SNI line up with the :authority header value.
            kwargs['server_hostname'] = hostname
            return ssl_context.old_wrap_socket(sock, *args, **kwargs)

        setattr(ssl_context, "wrap_socket", new_wrap_socket)
        # Opens a connection to the server.
        logging.info(f"Connecting to '{scheme}://{replay_server}' with request to '{authority}{path}'")
        sock = socket.create_connection(('127.0.0.1', port))
        sock = ssl_context.wrap_socket(sock)

        # Initiate an HTTP/2 connection.
        http2_connection = H2Connection()
        http2_connection.initiate_connection()
        # Initial SETTINGS frame, etc.
        sock.sendall(http2_connection.data_to_send())
        client = Http2Connection(sock, http2_connection)
        response, data_delays = client.send_request(request)
        if response.errors:
            try:
                if 'StreamReset' in response.errors:
                    http2_connection.reset_stream(stream_id)
                if 'ConnectionTerminated' in response.errors:
                    http2_connection.close_connection(last_stream_id=0)
            except StreamClosedError as err:
                logging.error(err)
            return 1
        else:
            client.close()
    except Exception as e:
        logging.error(f"Connection to '{replay_server}' initiated with request to "
                      f"'{scheme}://{authority}{path}' failed: {e}")
        traceback.print_exc(file=sys.stdout)
        return 1

    print_transaction(request, response)
    logging.info(f'Smallest delay: {min(data_delays)} ms')
    logging.info(f'Largest delay: {max(data_delays)} ms')
    average = statistics.mean(data_delays)
    logging.info(f'Average delay over {len(data_delays)} reads: {average} ms')
    isclose = math.isclose(average, write_timeout, rel_tol=0.2)
    if isclose:
        logging.info(f'Average delay of {average} is within 20% of the expected delay: {write_timeout} ms')
    else:
        logging.info(f'Average delay of {average} is not within 20% of the expected delay: {write_timeout} ms')

    return 0 if isclose else 1


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments.

    :return: The parsed arguments.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('hostname', type=str, help='The hostname to use for the :authority header.')
    parser.add_argument('port', type=int, help='The port to connect to.')
    parser.add_argument('cert', type=str, help='The TLS certificate file.')
    parser.add_argument('write_timeout', type=int, help='The expected maximum amount of time frames should be delivered.')
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose logging.')
    return parser.parse_args()


def main() -> int:
    """Start the client."""
    args = parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format='%(asctime)s - %(levelname)s - %(message)s')

    return send_http2_request_to_server(args.hostname, args.port, args.cert, args.write_timeout)


if __name__ == '__main__':
    sys.exit(main())
