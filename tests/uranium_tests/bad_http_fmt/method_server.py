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
"""Serve one HTTP transaction without validating the request method."""

import argparse
import socket
from pathlib import Path

RESPONSE = b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nX-Resp-Id: 3\r\n\r\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int, help="Port to listen on.")
    parser.add_argument("outfile", type=Path, help="File to write the received request to.")
    args = parser.parse_args()

    with socket.create_server(("127.0.0.1", args.port)) as server:
        # The test waits for this line, so the listener exists before ATS connects.
        print("listening", flush=True)
        conn, _ = server.accept()
        with conn:
            request = b""
            while b"\r\n\r\n" not in request:
                data = conn.recv(4096)
                if not data:
                    break
                request += data
            args.outfile.write_bytes(request)
            conn.sendall(RESPONSE)


if __name__ == "__main__":
    main()
