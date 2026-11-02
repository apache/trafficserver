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
"""Download a large object from ATS over TLS while reading the response body
slowly, to exercise HTTP tunnel flow control (proxy.config.http.flow_control)
on a TLS client connection.

A slow reader keeps ATS's client-side write buffer full, so the tunnel
repeatedly throttles the origin read and must unthrottle as the buffered data
drains to the client. The whole body must still arrive; a flow-control stall
(no unthrottle) shows up as a short or timed-out read. This path had no prior
gold coverage, and the layered TLS VConnection drives the unthrottle through its
demand-driven write rather than the inherited write-buffer-empty trap, so it is
worth guarding directly.
"""

import argparse
import socket
import ssl
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", type=int, required=True, help="ATS TLS port")
    parser.add_argument("--host", default="ex.test", help="Host header / SNI")
    parser.add_argument("--path", default="/obj", help="request path")
    parser.add_argument("--expect-bytes", type=int, required=True, help="expected body length")
    parser.add_argument("--read-size", type=int, default=16 * 1024, help="bytes per recv")
    parser.add_argument("--read-delay", type=float, default=0.002, help="sleep between recvs (s)")
    parser.add_argument("--recv-timeout", type=float, default=15.0, help="per-recv socket timeout (s)")
    parser.add_argument("--deadline", type=float, default=120.0, help="overall wall-clock budget (s)")
    args = parser.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    deadline = time.monotonic() + args.deadline
    body_received = 0
    error = ""
    try:
        with socket.create_connection(("127.0.0.1", args.port), timeout=args.recv_timeout) as sock:
            with ctx.wrap_socket(sock, server_hostname=args.host) as ssock:
                ssock.settimeout(args.recv_timeout)
                request = (f"GET {args.path} HTTP/1.1\r\nHost: {args.host}\r\n"
                           "Connection: close\r\n\r\n").encode()
                ssock.sendall(request)

                # Read until we have the full header block, keeping any body bytes
                # that arrive in the same recv.
                buf = b""
                while b"\r\n\r\n" not in buf:
                    if time.monotonic() > deadline:
                        raise TimeoutError("deadline reached reading response headers")
                    chunk = ssock.recv(args.read_size)
                    if not chunk:
                        raise ConnectionError("connection closed before headers complete")
                    buf += chunk
                header_blob, _, leftover = buf.partition(b"\r\n\r\n")
                body_received = len(leftover)

                # Drain the body slowly so ATS's client-side write buffer stays full
                # and the tunnel relies on the buffer-empty unthrottle.
                while body_received < args.expect_bytes:
                    if time.monotonic() > deadline:
                        raise TimeoutError(f"deadline reached after {body_received} body bytes")
                    time.sleep(args.read_delay)
                    chunk = ssock.recv(args.read_size)
                    if not chunk:
                        break
                    body_received += len(chunk)
    except Exception as exc:  # noqa: BLE001 - any failure is a test signal
        error = f"{type(exc).__name__}: {exc}"

    passed = error == "" and body_received == args.expect_bytes

    print(f"BODY_BYTES={body_received}")
    print(f"EXPECT_BYTES={args.expect_bytes}")
    if error:
        print(f"ERROR={error}")
    print(f"RESULT={'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
