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
import sys
import time


def main() -> int:
    host = sys.argv[1]
    port = int(sys.argv[2])

    # A small cacheable, chunked response that is compressible (text/*).
    headers = (
        b"HTTP/1.1 200 OK\r\n"
        b"Connection: close\r\n"
        b"Cache-Control: public, max-age=60\r\n"
        b"Content-Type: text/plain\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"\r\n")

    # Two chunks + zero-chunk terminator.
    chunk1 = b"1E\r\nThis is a small compressible body.\r\n\r\n"
    chunk2 = b"0\r\n\r\n"

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((host, port))
        s.listen(1)
        conn, _ = s.accept()
        with conn:
            # Read and ignore request; simple single transaction.
            _ = conn.recv(4096)
            conn.sendall(headers)
            conn.sendall(chunk1)
            time.sleep(0.01)
            conn.sendall(chunk2)
    return 0


if __name__ == '__main__':
    sys.exit(main())
