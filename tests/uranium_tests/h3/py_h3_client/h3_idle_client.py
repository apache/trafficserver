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
"""Hold an HTTP/3 request stream open and idle, then finish it.

The client sends a POST's headers, waits before sending its body, and then
sends one more request on the same connection. While it waits, the request
stream is open in both directions but neither side has anything to send.
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
    parser.add_argument("--idle-seconds", type=float, required=True, help="How long to hold the request stream open and idle")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    port = int(port_text)

    held_open = RequestCase("py-idle-open", b"POST", "/py-idle-open", request_size=100, response_size=100)
    after = RequestCase("py-idle-after", b"GET", "/py-idle-after", response_size=100)

    async with await connect_h3(host, port, args.server_name) as client:
        verify_response(held_open, await client.request(args.authority, held_open, body_delay=args.idle_seconds))
        print(f"ok {held_open.name}")

        verify_response(after, await client.request(args.authority, after))
        print(f"ok {after.name}")
    print("completed idle HTTP/3 checks")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
