#!/usr/bin/env python3
#
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
"""Run requests on many concurrent HTTP/3 connections, each from its own UDP port."""

from __future__ import annotations

import argparse
import asyncio

from h3_client import RequestCase, connect_h3, verify_response


async def run_connection(host: str, port: int, args: argparse.Namespace, request_case: RequestCase) -> bool:
    """Run every request on one connection and return whether the server made it retry."""
    async with await connect_h3(host, port, args.server_name) as client:
        for _ in range(args.requests):
            verify_response(request_case, await client.request(args.authority, request_case))
        # aioquic has no public view of whether the handshake went through a Retry.
        return client._quic._retry_count > 0


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument("--path", required=True, help="Request path")
    parser.add_argument("--response-size", type=int, required=True, help="Expected response body size")
    parser.add_argument("--connections", type=int, required=True, help="Number of concurrent connections")
    parser.add_argument("--requests", type=int, required=True, help="Requests on each connection")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    request_case = RequestCase(args.path.lstrip("/"), b"GET", args.path, response_size=args.response_size)
    retried = await asyncio.gather(*(run_connection(host, int(port_text), args, request_case) for _ in range(args.connections)))
    print(f"completed {args.requests} requests on each of {args.connections} HTTP/3 connections")
    print(f"{sum(retried)} connections were retried")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
