#!/usr/bin/env python3
"""
An HTTP/2 client that sends requests at a configurable rate.

This client is designed for testing rate limiting functionality in the
abuse_shield plugin. It sends a specified number of requests at a controlled
rate over a single HTTP/2 connection.
"""
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
import socket
import ssl
import sys
import time
from typing import Dict, Optional, Tuple

import h2.connection
import h2.events
import h2.exceptions


def get_socket(host: str, port: int, timeout: float = 30.0) -> socket.socket:
    """Create a TLS-wrapped socket with HTTP/2 ALPN.

    :param host: The target host.
    :param port: The target port.
    :param timeout: Socket timeout in seconds.
    :returns: A TLS-wrapped socket.
    """
    socket.setdefaulttimeout(timeout)

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])

    tls_socket = socket.create_connection((host, port))
    tls_socket = ctx.wrap_socket(tls_socket, server_hostname=host)
    return tls_socket


class H2RateClient:
    """HTTP/2 client that sends requests at a configurable rate."""

    def __init__(self, host: str, port: int, verbose: bool = False):
        """Initialize the client.

        :param host: Target host.
        :param port: Target port.
        :param verbose: Enable verbose logging.
        """
        self.host = host
        self.port = port
        self.verbose = verbose
        self.socket: Optional[socket.socket] = None
        self.h2_conn: Optional[h2.connection.H2Connection] = None
        self.next_stream_id = 1

        # Track responses per stream.
        self.responses: Dict[int, Tuple[int, bytes]] = {}
        self.pending_streams: set = set()

    def connect(self) -> None:
        """Establish the HTTP/2 connection."""
        logging.info(f"Connecting to {self.host}:{self.port}")
        self.socket = get_socket(self.host, self.port)
        self.h2_conn = h2.connection.H2Connection()
        self.h2_conn.initiate_connection()
        self.socket.sendall(self.h2_conn.data_to_send())
        logging.debug("HTTP/2 connection established")

    def close(self) -> None:
        """Close the HTTP/2 connection."""
        if self.h2_conn:
            self.h2_conn.close_connection()
            if self.socket:
                try:
                    self.socket.sendall(self.h2_conn.data_to_send())
                except (BrokenPipeError, ConnectionResetError):
                    pass
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        logging.debug("Connection closed")

    def send_request(self, path: str) -> int:
        """Send a single HTTP/2 request.

        :param path: The request path.
        :returns: The stream ID used for this request.
        """
        stream_id = self.next_stream_id
        self.next_stream_id += 2

        headers = [
            (':method', 'GET'),
            (':path', path),
            (':authority', self.host),
            (':scheme', 'https'),
        ]

        try:
            self.h2_conn.send_headers(stream_id, headers, end_stream=True)
            self.socket.sendall(self.h2_conn.data_to_send())
            self.pending_streams.add(stream_id)
            logging.debug(f"Sent request on stream {stream_id}: GET {path}")
        except (BrokenPipeError, ConnectionResetError, h2.exceptions.ProtocolError) as e:
            logging.warning(f"Failed to send request on stream {stream_id}: {e}")
            return -1

        return stream_id

    def receive_responses(self, timeout: float = 0.01) -> int:
        """Receive and process any pending responses.

        :param timeout: How long to wait for data.
        :returns: Number of responses received.
        """
        responses_received = 0
        self.socket.setblocking(False)

        try:
            while True:
                try:
                    data = self.socket.recv(65536)
                    if not data:
                        break

                    events = self.h2_conn.receive_data(data)
                    for event in events:
                        if isinstance(event, h2.events.ResponseReceived):
                            status = None
                            for header in event.headers:
                                if header[0] == b':status' or header[0] == ':status':
                                    status = int(header[1])
                                    break
                            if event.stream_id not in self.responses:
                                self.responses[event.stream_id] = (status, b'')
                            else:
                                self.responses[event.stream_id] = (status, self.responses[event.stream_id][1])
                            logging.debug(f"Response headers on stream {event.stream_id}: status={status}, headers={event.headers}")

                        elif isinstance(event, h2.events.DataReceived):
                            self.h2_conn.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                            if event.stream_id in self.responses:
                                status, body = self.responses[event.stream_id]
                                self.responses[event.stream_id] = (status, body + event.data)

                        elif isinstance(event, h2.events.StreamEnded):
                            if event.stream_id in self.pending_streams:
                                self.pending_streams.remove(event.stream_id)
                            responses_received += 1
                            logging.debug(f"Stream {event.stream_id} ended")

                        elif isinstance(event, h2.events.StreamReset):
                            if event.stream_id in self.pending_streams:
                                self.pending_streams.remove(event.stream_id)
                            responses_received += 1
                            logging.debug(f"Stream {event.stream_id} reset with error {event.error_code}")

                        elif isinstance(event, h2.events.ConnectionTerminated):
                            logging.warning(f"Connection terminated: error={event.error_code}")
                            self.pending_streams.clear()
                            return responses_received

                    self.socket.sendall(self.h2_conn.data_to_send())

                except (BlockingIOError, ssl.SSLWantReadError, ssl.SSLWantWriteError):
                    break
                except (ConnectionResetError, BrokenPipeError, ssl.SSLError) as e:
                    logging.warning(f"Connection error while receiving: {e}")
                    break

        finally:
            self.socket.setblocking(True)

        return responses_received

    def run(self, num_requests: int, rate: float, path: str) -> Tuple[int, int, int]:
        """Send requests at the specified rate.

        :param num_requests: Total number of requests to send.
        :param rate: Requests per second.
        :param path: Request path.
        :returns: Tuple of (requests_sent, successful_responses, failed_responses).
        """
        self.connect()

        interval = 1.0 / rate if rate > 0 else 0
        requests_sent = 0
        start_time = time.time()

        logging.info(f"Sending {num_requests} requests at {rate} req/sec to {path}")

        try:
            for i in range(num_requests):
                request_start = time.time()

                stream_id = self.send_request(path)
                if stream_id > 0:
                    requests_sent += 1

                # Process any pending responses.
                self.receive_responses()

                # After the first request, add a small delay to allow caching.
                if i == 0:
                    time.sleep(0.1)

                # Sleep to maintain the target rate.
                elapsed = time.time() - request_start
                sleep_time = interval - elapsed
                if sleep_time > 0:
                    time.sleep(sleep_time)

            # Wait for remaining responses with a timeout.
            wait_start = time.time()
            max_wait = 5.0
            while self.pending_streams and (time.time() - wait_start) < max_wait:
                self.receive_responses(timeout=0.1)
                time.sleep(0.01)

        except KeyboardInterrupt:
            logging.info("Interrupted")
        finally:
            self.close()

        total_time = time.time() - start_time
        actual_rate = requests_sent / total_time if total_time > 0 else 0

        successful = sum(1 for (status, _) in self.responses.values() if status and 200 <= status < 400)
        failed = len(self.responses) - successful

        logging.info(f"Completed: {requests_sent} requests sent in {total_time:.2f}s ({actual_rate:.1f} req/sec)")
        logging.info(f"Responses: {successful} successful, {failed} failed, {len(self.pending_streams)} pending")

        return requests_sent, successful, failed


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='HTTP/2 client that sends requests at a configurable rate.')
    parser.add_argument('--host', type=str, default='localhost', help='Target host (default: localhost)')
    parser.add_argument('--port', type=int, required=True, help='Target port')
    parser.add_argument('--num-requests', type=int, required=True, help='Total number of requests to send')
    parser.add_argument('--rate', type=float, required=True, help='Requests per second')
    parser.add_argument('--path', type=str, default='/', help='Request path (default: /)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose logging')
    return parser.parse_args()


def main() -> int:
    """Run the rate-controlled HTTP/2 client."""
    args = parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format='%(asctime)s - %(levelname)s - %(message)s')

    client = H2RateClient(args.host, args.port, args.verbose)
    requests_sent, successful, failed = client.run(args.num_requests, args.rate, args.path)

    print(f"Requests sent: {requests_sent}")
    print(f"Successful responses: {successful}")
    print(f"Failed responses: {failed}")

    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
