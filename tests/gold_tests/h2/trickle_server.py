'''
Implement an HTTP/2 server that monitors DATA frame statistics.
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

import argparse
import logging
import math
import statistics
import sys
import time
from OpenSSL.SSL import Error as SSLError
from OpenSSL.SSL import SysCallError as SSLSysCallError
import threading

import eventlet
from eventlet.green.OpenSSL import SSL, crypto
from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import StreamEnded, RequestReceived, DataReceived, StreamReset, ConnectionTerminated
from h2.errors import ErrorCodes as H2ErrorCodes
from h2.exceptions import StreamClosedError, StreamIDTooLowError

from typing import Dict, List, Optional, Set


def get_body_text() -> bytes:
    """Create a body of text to send in the response."""
    body_items: List[str] = []
    for i in range(100):
        # Create a chunk of 0 padded bytes, followed by a space.
        chunk_payload = f'{i:06x} '.encode("utf-8")
        body_items.append(chunk_payload)
    return b''.join(body_items)


class RequestInfo:
    """POD for request headers, etc."""

    def __init__(self, stream_id: int):
        self.stream_id: int = stream_id
        self.headers: Dict[bytes, bytes] = None
        self.body_bytes: bytes = None


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


class Http2ConnectionManager:
    """Manages a single HTTP/2 connection."""

    def __init__(self, sock: eventlet.greenio.GreenSocket):
        listening_config = H2Configuration(client_side=False, validate_inbound_headers=False)
        self.tls = threading.local()
        self.sock = sock
        self.sock.settimeout(1.0)
        self.listening_conn: H2Connection = H2Connection(config=listening_config)
        self.requests: Dict[int, RequestInfo] = {}
        self.completed_stream_ids: Set[int] = set()

        # Delay times in ms between each data frame.
        self._data_delays: List[int] = []
        # The last time in ms since epoch that a packet was received.
        self.last_packet_time: int = 0

    def _send_responses(self, responses: Dict[int, ResponseInfo]) -> None:
        """Send any responses that have been generated.

        :param responses: A dictionary of responses we wish to send.
        """
        responded_streams = []
        for stream_id, response in responses.items():
            try:
                self.listening_conn.send_headers(stream_id, response.headers)

                send_window = self.listening_conn.local_flow_control_window(stream_id)
                body_size = len(response.body_bytes)
                bytes_to_send = min(send_window, body_size)
                if bytes_to_send < body_size:
                    raise ValueError(
                        f'We do not have a big enough window: body size of {body_size} bytes vs {send_window} byte window')
                # Send one byte at a time, every millisecond.
                bytes_sent = 0
                while bytes_to_send > 0:
                    chunk_size = 1
                    byte_to_send = response.body_bytes[bytes_sent:bytes_sent + chunk_size]
                    logging.debug(f'Sending {byte_to_send}')
                    self.listening_conn.send_data(stream_id, byte_to_send)
                    self.sock.sendall(self.listening_conn.data_to_send())
                    bytes_sent += chunk_size
                    bytes_to_send -= chunk_size
                    time.sleep(0.001)

                self.listening_conn.send_data(stream_id, response.body_bytes, end_stream=False if response.trailers else True)
                if response.trailers is not None:
                    self.listening_conn.send_headers(stream_id, response.trailers, end_stream=True)
                responded_streams.append(stream_id)

            except StreamClosedError as e:
                logging.debug(e)
            except StreamIDTooLowError as e:
                logging.debug(e)
        try:
            # Send the responses we added to the listening_conn.
            self.sock.sendall(self.listening_conn.data_to_send())
        except (SSLError, SSLSysCallError) as e:
            logging.debug(f'Ignoring sock.sendall exception for now: {e}')

        # Clean up any responses we sent.
        for stream_id in responded_streams:
            del responses[stream_id]

    def _receive_data(self, responses: Dict[int, ResponseInfo]) -> Optional[bytes]:
        """Receive data from the socket.

        :param responses: A dictionary of stream IDs to responses that have accumulated.

        :return: The data received, or None if the connection for the socket has closed.
        """
        data: Optional[bytes] = None
        while not data:
            try:
                logging.debug('Receiving data on the socket.')
                data = self.sock.recv(65535)
            except SSLError:
                logging.debug('recv error: the socket is closed.')
                return None
            except TimeoutError:
                # Take time to send any responses we've generated.
                self._send_responses(responses)

                # Loop back around to receive more data.
                logging.debug('Timeout, waiting again for more data.')
                continue
        return data

    def _process_events(self, events: List, responses: Dict[int, ResponseInfo]) -> None:
        """Process events from the H2 connection.

        :param events: The events to process.
        :param responses: A dictionary of stream IDs to responses that have accumulated.
        """
        have_counted_data_delay = False
        for event in events:
            if hasattr(event, 'stream_id'):
                stream_id = event.stream_id
                if stream_id not in self.requests:
                    self.requests[stream_id] = RequestInfo(stream_id)

                request_info = self.requests[stream_id]

                if isinstance(event, DataReceived):
                    if request_info.body_bytes is None:
                        request_info.body_bytes = b''
                    logging.debug(f'Got data for stream {stream_id}: {event.data.decode()}')
                    request_info.body_bytes += event.data

                    if not have_counted_data_delay:
                        ms_since_last_packet = (time.perf_counter_ns() - self.last_packet_time) / (1000 * 1000)
                        logging.debug(f'Counting data delay for stream {stream_id}: {ms_since_last_packet} ms')
                        self._data_delays.append(ms_since_last_packet)
                        self.last_packet_time = time.perf_counter_ns()
                        have_counted_data_delay = True

                if isinstance(event, RequestReceived):
                    logging.info(f'Incoming request received for stream {event.stream_id}.')
                    logging.debug(f'Headers received: {event.headers}')
                    request_info.headers = event.headers
                    self.last_packet_time = time.perf_counter_ns()

                if isinstance(event, StreamReset):
                    self.completed_stream_ids.add(stream_id)
                    err = H2ErrorCodes(event.error_code).name
                    logging.debug(f'Received RST_STREAM frame with error code {err} on stream {event.stream_id}.')
                    if stream_id not in responses.keys():
                        response = self._process_request(request_info)
                        if response is not None:
                            responses[stream_id] = response

                if isinstance(event, StreamEnded):
                    logging.debug('StreamEnded')
                    self.completed_stream_ids.add(stream_id)
                    if stream_id not in responses.keys():
                        response = self._process_request(request_info)
                        if response is not None:
                            responses[stream_id] = response

            else:
                if isinstance(event, ConnectionTerminated):
                    err = H2ErrorCodes(event.error_code).name
                    logging.debug(f'Received GOAWAY frame with error code {err} on with last stream id {event.last_stream_id}.')
                    self.listening_conn.close_connection()

    def _cleanup_closed_stream_ids(self) -> None:
        """Clean up any closed streams."""
        for stream_id in self.completed_stream_ids:
            try:
                if self.listening_conn.streams[stream_id].closed:
                    del self.requests[stream_id]
            except KeyError:
                pass
        try:
            self.completed_stream_ids = set([id for id in self.completed_stream_ids if not self.listening_conn.streams[id].closed])
        except KeyError:
            pass

    def run_forever(self):
        self.listening_conn.initiate_connection()

        try:
            self.sock.sendall(self.listening_conn.data_to_send())
        except (SSLError, SSLSysCallError) as e:
            logging.debug(f'Initial sock.sendall exception: {e}')
            return

        responses: Dict[int, ResponseInfo] = {}
        while True:
            data = self._receive_data(responses)
            if not data:
                # Connection ended.
                break

            logging.debug(f'Giving {len(data)} bytes to the h2 connection')
            events = self.listening_conn.receive_data(data)
            self._process_events(events, responses)
            self._cleanup_closed_stream_ids()
            self._send_responses(responses)

            logging.debug('Sending data on the socket')
            try:
                self.sock.sendall(self.listening_conn.data_to_send())
            except (SSLError, SSLSysCallError) as e:
                logging.debug(f'Ignoring end-loop sock.sendall exception for now: {e}')
                pass

    def get_data_delays(self) -> List[int]:
        """Get the DATA frame timing list.

        :return: The list of DATA frame timings.
        """
        return self._data_delays

    def _process_request(self, request: RequestInfo) -> ResponseInfo:
        """Handle a request from a client.

        :return: A response to send back to the client.
        """
        logging.debug(f'Request received for stream id: {request.stream_id}')
        response_headers = [
            (':status'.encode(), '200'.encode()),
            ('content-type'.encode(), 'text/plain'.encode()),
        ]
        response = ResponseInfo(200, response_headers, get_body_text())

        self._print_transaction(request, response)
        return response

    def _print_transaction(self, request: RequestInfo, response: ResponseInfo) -> None:
        """Print the details of the request and response."""

        description = ''
        description += '\n==== REQUEST HEADERS ====\n'
        for k, v in request.headers:
            if isinstance(k, bytes):
                k, v = (k.decode('ascii'), v.decode('ascii'))
            description += f"{k}: {v}\n"

        if request.body_bytes is not None:
            description += f"\n==== REQUEST BODY ====\n{request.body_bytes.decode()}\n"

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
            for k, v in response.trailers:
                if isinstance(k, bytes):
                    k, v = (k.decode('ascii'), v.decode('ascii'))
                description += f"{k}: {k}\n"
        description += "\n==== END ====\n"
        logging.info(description)


def alpn_callback(conn, protos):
    """The OpenSSL callback for selecting the protocol."""
    if b'h2' in protos:
        return b'h2'

    raise RuntimeError("No acceptable protocol offered!")


def servername_callback(conn):
    """The OpenSSL callback for inspecting the SNI."""
    sni = conn.get_servername()
    conn.set_app_data({'sni': sni})
    logging.info(f"Got SNI from client: {sni}")


def run_server(listen_port, https_pem, ca_pem) -> List[int]:
    """Run the HTTP/2 server.

    :param listen_port: The port to listen on.
    :param https_pem: The path to the certificate key.
    :param ca_pem: The path to the CA certificate.

    :return: The list of DATA frame delays.
    """
    options = (SSL.OP_NO_COMPRESSION | SSL.OP_NO_SSLv2 | SSL.OP_NO_SSLv3 | SSL.OP_NO_TLSv1 | SSL.OP_NO_TLSv1_1)
    context = SSL.Context(SSL.TLSv1_2_METHOD)
    context.set_options(options)
    context.set_verify(SSL.VERIFY_NONE, lambda *args: True)
    context.use_privatekey_file(https_pem)
    context.use_certificate_file(https_pem)
    context.set_alpn_select_callback(alpn_callback)
    context.set_tlsext_servername_callback(servername_callback)
    context.set_cipher_list("RSA+AESGCM".encode())
    context.set_tmp_ecdh(crypto.get_elliptic_curve('prime256v1'))

    listening_socket = eventlet.listen(('0.0.0.0', listen_port))
    listening_socket = SSL.Connection(context, listening_socket)
    logging.info(f"Serving HTTP/2 Proxy on 127.0.0.1:{listen_port} with pem '{https_pem}'")
    pool = eventlet.GreenPool()

    while True:
        try:
            new_connection_socket, _ = listening_socket.accept()
            manager = Http2ConnectionManager(new_connection_socket)
            manager.cert_file = https_pem
            manager.ca_file = ca_pem
            pool.spawn_n(manager.run_forever)
        except KeyboardInterrupt as e:
            logging.debug("Handling KeyboardInterrupt")
            return manager.get_data_delays()
        except SystemExit:
            break


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('listen_port', type=int, help='Port to listen on.')
    parser.add_argument('cert_key', type=str, help='Path to the certificate key.')
    parser.add_argument('ca_cert', type=str, help='Path to the CA certificate.')
    parser.add_argument('write_timeout', type=int, help='The timeout between sending frames.')
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose logging.')
    return parser.parse_args()


def main() -> int:
    """Start the HTTP/2 server."""
    args = parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format='%(asctime)s - %(levelname)s - %(message)s')

    data_delays = run_server(args.listen_port, args.cert_key, args.ca_cert)
    logging.info(f'Smallest delay: {min(data_delays)} ms')
    logging.info(f'Largest delay: {max(data_delays)} ms')
    average = statistics.mean(data_delays)
    logging.info(f'Average delay over {len(data_delays)} reads: {average} ms')
    isclose = math.isclose(average, args.write_timeout, rel_tol=0.2)
    if isclose:
        logging.info(f'Average delay of {average} is within 20% of the expected delay: {args.write_timeout} ms')
    else:
        logging.info(f'Average delay of {average} is not within 20% of the expected delay: {args.write_timeout} ms')
    return 0 if isclose else 1


if __name__ == '__main__':
    sys.exit(main())
