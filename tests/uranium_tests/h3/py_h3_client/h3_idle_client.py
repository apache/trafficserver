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
"""Send one HTTP/3 request, then leave the connection silent for a while.

aioquic sends no keep-alive PINGs, so after the response nothing crosses the
connection until the quiet period ends.
"""

from __future__ import annotations

import argparse
import asyncio

from h3_client import RequestCase, connect_h3, verify_response


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument("--quiet", type=float, required=True, help="Seconds to stay silent after the first response")
    parser.add_argument(
        "--request-after-quiet",
        action="store_true",
        help="Send a second request on the same connection once the quiet period ends",
    )
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    request_case = RequestCase("py-get-small", b"GET", "/py-get-small", response_size=100)
    async with await connect_h3(host, int(port_text), args.server_name) as client:
        verify_response(request_case, await client.request(args.authority, request_case))
        # aioquic has no public view of the connection ID the server chose. It
        # names the server's qlog file for this connection.
        print(f"server connection id {client._quic._peer_cid.cid.hex()}", flush=True)

        await asyncio.sleep(args.quiet)

        if args.request_after_quiet:
            if client._terminated.done():
                terminated = client._terminated.result()
                raise AssertionError(f"connection closed during the quiet period: {terminated.reason_phrase}")
            verify_response(request_case, await client.request(args.authority, request_case))
            print(f"ok request after {args.quiet:g} seconds of silence")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
