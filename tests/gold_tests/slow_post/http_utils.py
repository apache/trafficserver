#!/usr/bin/env python3
"""Common logic between the ad hoc client and server."""

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


def wait_for_headers_complete(sock: socket.socket) -> bytes:
    """Wait for the headers to be complete.

    :param sock: The socket to read from.
    :returns: The bytes read off the socket.
    """
    headers = b""
    while True:
        data = sock.recv(1024)
        if not data:
            print("Socket closed.")
            break
        print(f'Received:\n{data}')
        headers += data
        if b"\r\n\r\n" in headers:
            break
    return headers


def determine_outstanding_bytes_to_read(read_bytes: bytes) -> int:
    """Determine how many more bytes to read from the headers.

    This parses the Content-Length header to determine how many more bytes to
    read.

    :param read_bytes: The bytes read so far.
    :returns: The number of bytes to read, or -1 if it is chunked encoded.
    """
    headers = read_bytes.decode("utf-8").split("\r\n")
    content_length_value = None
    for header in headers:
        if header.lower().startswith("content-length:"):
            content_length_value = int(header.split(":")[1].strip())
        elif header.lower().startswith("transfer-encoding: chunked"):
            return -1
    if content_length_value is None:
        raise ValueError("No Content-Length header found.")

    end_of_headers = read_bytes.find(b"\r\n\r\n")
    if end_of_headers == -1:
        raise ValueError("No end of headers found.")

    end_of_headers += 4
    return content_length_value - (len(read_bytes) - end_of_headers)


def drain_socket(sock: socket.socket, previously_read_data: bytes, num_bytes_to_drain: int) -> None:
    """Read the rest of the request.

    :param sock: The socket to drain.
    :param num_bytes_to_drain: The number of bytes to drain. If -1, then drain
    bytes until the final zero-length chunk is read.
    """

    read_data = previously_read_data
    num_bytes_drained = 0
    while True:
        if num_bytes_to_drain > 0:
            if num_bytes_drained >= num_bytes_to_drain:
                break
        elif b'0\r\n\r\n' == read_data[-5:]:
            print("Found end of chunked data.")
            break

        data = sock.recv(1024)
        print(f'Received:\n{data}')
        if not data:
            print("Socket closed.")
            break
        num_bytes_drained += len(data)
        read_data += data
