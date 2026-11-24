#!/usr/bin/env python3
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may
#  obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Open an HTTP/3 connection, then leave it silent for a while.

The client sends one request, or none with --no-request, and goes silent after
the response or the handshake. aioquic sends no keep-alive PINGs, so nothing
crosses the connection until the quiet period ends. When the server advertises
a shorter idle timeout than aioquic's own 60 s, aioquic adopts it and expires
its side silently at about the same time as the server. With
--request-after-quiet it sends a second request once the quiet period ends.
"""

from __future__ import annotations

import argparse
import asyncio

from h3_client import RequestCase, connect_h3, verify_response

# Seconds to wait for each response. This is deliberately tighter than the 30 s
# inside request(), so a hung response fails here, with output, before the
# harness kills the client.
RESPONSE_TIMEOUT = 5


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument(
        "--quiet",
        type=float,
        required=True,
        help="Seconds to stay silent after the first response, or after the handshake with --no-request",
    )
    parser.add_argument(
        "--request-after-quiet",
        action="store_true",
        help="Send a second request on the same connection once the quiet period ends",
    )
    parser.add_argument(
        "--no-request",
        action="store_true",
        help="Send no request; stay silent from the end of the handshake",
    )
    args = parser.parse_args()
    if args.no_request and args.request_after_quiet:
        parser.error("--no-request and --request-after-quiet are mutually exclusive")

    host, port_text = args.addr.rsplit(":", 1)
    request_case = RequestCase("py-get-small", b"GET", "/py-get-small", response_size=100)
    async with await connect_h3(host, int(port_text), args.server_name) as client:
        if not args.no_request:
            verify_response(request_case, await asyncio.wait_for(client.request(args.authority, request_case), RESPONSE_TIMEOUT))
        # aioquic has no public accessor for the server's connection ID, so read
        # _peer_cid. ATS names its qlog for this connection after that ID.
        print(f"server connection id {client._quic._peer_cid.cid.hex()}", flush=True)
        # Read the server's max_idle_timeout transport parameter the same way.
        # It is in seconds, and None means the server did not send one.
        print(f"server max_idle_timeout {client._quic._remote_max_idle_timeout}", flush=True)

        await asyncio.sleep(args.quiet)

        if args.request_after_quiet:
            if client._terminated.done():
                terminated = client._terminated.result()
                raise AssertionError(f"connection closed during the quiet period: {terminated.reason_phrase}")
            verify_response(request_case, await asyncio.wait_for(client.request(args.authority, request_case), RESPONSE_TIMEOUT))
            print(f"ok request after {args.quiet:g} seconds of silence")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
