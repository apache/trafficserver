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
'''
Client that sends a request, then drains the response one small sip at a time.

Reading a few bytes per interval keeps continuous backpressure on ATS's
client-side buffer, so it stays above the water mark while the origin's
last-chunk arrives and the chunked throttle parks it unparsed. Prints the status
line and the body length it received.
'''

import argparse
import socket
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--ip", default="127.0.0.1")
    ap.add_argument("--method", default="POST")
    ap.add_argument("--stall", type=float, default=1.0, help="seconds to wait before the first read")
    ap.add_argument("--sip", type=int, default=512, help="bytes to read per interval")
    ap.add_argument("--interval", type=float, default=0.02, help="seconds between sips")
    ap.add_argument("--rcvbuf", type=int, default=2048)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, args.rcvbuf)
    sock.settimeout(30)
    sock.connect((args.ip, args.port))

    body = b"data" if args.method == "POST" else b""
    request = (
        f"{args.method} /dechunk HTTP/1.1\r\n"
        f"Host: www.example.com\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n").encode() + body
    sock.sendall(request)

    time.sleep(args.stall)

    response = b""
    try:
        while True:
            chunk = sock.recv(args.sip)
            if not chunk:
                break
            response += chunk
            time.sleep(args.interval)
    except (ConnectionResetError, socket.timeout) as e:
        print(f"read error: {e!r}")
    sock.close()

    head, sep, payload = response.partition(b"\r\n\r\n")
    status = head.split(b"\r\n", 1)[0].decode(errors="replace") if head else "<no response>"
    print(f"status: {status}")
    print(f"body bytes: {len(payload) if sep else 0}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
