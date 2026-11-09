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
"""Send HTTP/3 requests whose FIN arrives with the headers or after the response.

Each request stream ID is printed as ``stream <id> <ordering>`` so that the
test can check that Traffic Server reclaimed every one of them while the
connection was still open.
"""

from __future__ import annotations

import argparse
import asyncio

from h3_client import H3ClientProtocol, ResponseState, connect_h3

# How long the connection stays open after the last request, so that Traffic
# Server handles the final FIN before the connection is torn down.
LINGER_SECONDS = 2

RESPONSE_SIZE = 100


class ReclamationClient(H3ClientProtocol):
    """Adds a request whose stream is finished only after its response arrives."""

    async def get(self, authority: str, path: str, fin_after_response: bool) -> tuple[int, ResponseState]:
        """Send a bodyless GET and return its stream ID and response.

        :param fin_after_response: Leave the request stream open until the
            whole response has been received, then finish it with an empty
            STREAM frame, so that Traffic Server sees its write side complete
            before its read side.
        """
        stream_id = self._quic.get_next_available_stream_id()
        waiter: asyncio.Future[ResponseState] = asyncio.get_running_loop().create_future()
        self._waiters[stream_id] = waiter

        headers = [
            (b":method", b"GET"),
            (b":scheme", b"https"),
            (b":authority", authority.encode()),
            (b":path", path.encode()),
            (b"user-agent", b"ats-h3-stream-reclamation"),
        ]
        self._http.send_headers(stream_id, headers, end_stream=not fin_after_response)
        self.transmit()

        response = await asyncio.wait_for(waiter, timeout=10)
        if fin_after_response:
            self._quic.send_stream_data(stream_id, b"", end_stream=True)
            self.transmit()
        return stream_id, response


async def run(host: str, port: int, authority: str, server_name: str, count: int) -> None:
    async with await connect_h3(host, port, server_name, ReclamationClient) as client:
        for fin_after_response in (False, True):
            ordering = "fin-after-response" if fin_after_response else "fin-with-headers"
            for i in range(count):
                path = f"/nocache/{RESPONSE_SIZE}/{ordering}-{i}"
                stream_id, response = await client.get(authority, path, fin_after_response)
                if response.status != 200 or len(response.body) != RESPONSE_SIZE:
                    raise AssertionError(f"{ordering}-{i}: got status {response.status} with {len(response.body)} body bytes")
                print(f"stream {stream_id} {ordering}", flush=True)
        await asyncio.sleep(LINGER_SECONDS)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument("--count", type=int, default=5, help="Requests to send for each FIN ordering")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    asyncio.run(run(host, int(port_text), args.authority, args.server_name, args.count))


if __name__ == "__main__":
    main()
