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
import ssl

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import QuicEvent


class H3Client(QuicConnectionProtocol):
    """HTTP/3 client protocol that collects one response per request stream."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._http = H3Connection(self._quic)
        self._status: dict[int, int] = {}
        self._body: dict[int, bytearray] = {}
        self._waiters: dict[int, asyncio.Future[tuple[int, bytes]]] = {}

    def quic_event_received(self, event: QuicEvent) -> None:
        for http_event in self._http.handle_event(event):
            if isinstance(http_event, HeadersReceived):
                for name, value in http_event.headers:
                    if name == b":status":
                        self._status[http_event.stream_id] = int(value)
            elif isinstance(http_event, DataReceived):
                self._body.setdefault(http_event.stream_id, bytearray()).extend(http_event.data)
            else:
                continue
            if http_event.stream_ended:
                waiter = self._waiters.pop(http_event.stream_id, None)
                if waiter is not None and not waiter.done():
                    status = self._status.get(http_event.stream_id, 0)
                    waiter.set_result((status, bytes(self._body.get(http_event.stream_id, b""))))

    async def get(self, authority: str, path: str) -> tuple[int, bytes]:
        stream_id = self._quic.get_next_available_stream_id()
        waiter: asyncio.Future[tuple[int, bytes]] = asyncio.get_running_loop().create_future()
        self._waiters[stream_id] = waiter
        self._http.send_headers(
            stream_id,
            [
                (b":method", b"GET"),
                (b":scheme", b"https"),
                (b":authority", authority.encode()),
                (b":path", path.encode()),
            ],
            end_stream=True,
        )
        self.transmit()
        return await asyncio.wait_for(waiter, timeout=30)


async def run_connection(args: argparse.Namespace, host: str, port: int) -> None:
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN, server_name=args.server_name)
    configuration.verify_mode = ssl.CERT_NONE
    async with connect(host, port, configuration=configuration, create_protocol=H3Client) as client:
        for _ in range(args.requests):
            status, body = await client.get(args.authority, args.path)
            if status != 200 or body != args.body.encode():
                raise RuntimeError(f"unexpected response: status={status} body={body!r}")


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument("--path", required=True, help="Request path")
    parser.add_argument("--body", required=True, help="Expected response body")
    parser.add_argument("--connections", type=int, required=True, help="Number of concurrent connections")
    parser.add_argument("--requests", type=int, required=True, help="Requests on each connection")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    await asyncio.gather(*(run_connection(args, host, int(port_text)) for _ in range(args.connections)))
    print(f"completed {args.requests} requests on each of {args.connections} HTTP/3 connections")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
