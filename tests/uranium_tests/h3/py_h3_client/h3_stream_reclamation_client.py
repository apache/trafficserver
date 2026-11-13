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
"""Send HTTP/3 requests that vary when each request stream's FIN arrives.

Each request stream ID is printed as ``stream <id> <ordering>`` so that the
test can check that Traffic Server reclaimed every one of them while the
connection was still open. With ``--access-log``, the client also waits, with
the connection still open, for every request path to appear in that log and
prints ``unlogged <path>`` for each one that does not.
"""

from __future__ import annotations

import argparse
import asyncio
import itertools
import time
from pathlib import Path

from h3_client import H3ClientProtocol, ResponseState, connect_h3

# How long the connection stays open after the last request, so that Traffic
# Server handles the final FIN before the connection is torn down.
LINGER_SECONDS = 2

RESPONSE_SIZE = 100

# The fin-after-headers requests race the request FIN against the end of the
# response. Small responses finish within a few milliseconds, so FIN delays in
# that range make Traffic Server see both in the same pass for some requests.
RACE_FIN_DELAYS_MS = (0, 0.5, 1, 2, 3, 5)
RACE_RESPONSE_SIZES = (100, 1000, 10000)
RACE_CONCURRENCY = 8

# How long to wait for the access log to show every request. Traffic Server
# flushes it once a second in the test configuration.
ACCESS_LOG_TIMEOUT_SECONDS = 15


class ReclamationClient(H3ClientProtocol):
    """Adds requests whose stream is finished separately from the headers."""

    async def get(self, authority: str, path: str, fin_after_response: bool) -> tuple[int, ResponseState]:
        """Send a bodyless GET and return its stream ID and response.

        :param fin_after_response: Leave the request stream open until the
            whole response has been received, then finish it with an empty
            STREAM frame, so that Traffic Server sees its write side complete
            before its read side.
        """
        stream_id = self._send_get(authority, path, end_stream=not fin_after_response)
        response = await asyncio.wait_for(self._waiters[stream_id], timeout=10)
        if fin_after_response:
            self._finish_stream(stream_id)
        return stream_id, response

    async def get_fin_delayed(self, authority: str, path: str, fin_delay_ms: float) -> tuple[int, ResponseState]:
        """Send a bodyless GET whose FIN follows the headers after a short delay.

        :param fin_delay_ms: Milliseconds between the headers and the empty
            STREAM frame that finishes the request, whether or not the response
            has arrived by then.
        """
        stream_id = self._send_get(authority, path, end_stream=False)
        await asyncio.sleep(fin_delay_ms / 1000)
        self._finish_stream(stream_id)
        response = await asyncio.wait_for(self._waiters[stream_id], timeout=10)
        return stream_id, response

    def _send_get(self, authority: str, path: str, end_stream: bool) -> int:
        stream_id = self._quic.get_next_available_stream_id()
        self._waiters[stream_id] = asyncio.get_running_loop().create_future()
        headers = [
            (b":method", b"GET"),
            (b":scheme", b"https"),
            (b":authority", authority.encode()),
            (b":path", path.encode()),
            (b"user-agent", b"ats-h3-stream-reclamation"),
        ]
        self._http.send_headers(stream_id, headers, end_stream=end_stream)
        self.transmit()
        return stream_id

    def _finish_stream(self, stream_id: int) -> None:
        self._quic.send_stream_data(stream_id, b"", end_stream=True)
        self.transmit()


def check_response(name: str, response: ResponseState, size: int) -> None:
    if response.status != 200 or len(response.body) != size:
        raise AssertionError(f"{name}: got status {response.status} with {len(response.body)} body bytes, expected {size}")


async def run_fin_after_headers(client: ReclamationClient, authority: str, count: int) -> list[str]:
    """Send requests that race their FIN against the end of the response.

    :param count: Number of requests to send.
    :returns: The request paths, for the access log check.
    """
    limit = asyncio.Semaphore(RACE_CONCURRENCY)
    combinations = itertools.cycle(itertools.product(RACE_FIN_DELAYS_MS, RACE_RESPONSE_SIZES))

    async def one(i: int, fin_delay_ms: float, size: int) -> str:
        path = f"/nocache/{size}/fin-after-headers-{i}"
        async with limit:
            stream_id, response = await client.get_fin_delayed(authority, path, fin_delay_ms)
        check_response(path, response, size)
        print(f"stream {stream_id} fin-after-headers", flush=True)
        return path

    return list(await asyncio.gather(*(one(i, *next(combinations)) for i in range(count))))


async def wait_for_access_log(access_log: Path, paths: list[str]) -> list[str]:
    """Wait for every request path to be logged and return the paths that are not."""
    deadline = time.monotonic() + ACCESS_LOG_TIMEOUT_SECONDS
    while True:
        logged = set(access_log.read_text(errors="replace").splitlines()) if access_log.exists() else set()
        missing = [path for path in paths if path not in logged]
        if not missing or time.monotonic() >= deadline:
            return missing
        await asyncio.sleep(0.25)


async def run(host: str, port: int, authority: str, server_name: str, count: int, race_count: int, access_log: Path | None) -> None:
    async with await connect_h3(host, port, server_name, ReclamationClient) as client:
        paths: list[str] = []
        for fin_after_response in (False, True):
            ordering = "fin-after-response" if fin_after_response else "fin-with-headers"
            for i in range(count):
                path = f"/nocache/{RESPONSE_SIZE}/{ordering}-{i}"
                stream_id, response = await client.get(authority, path, fin_after_response)
                check_response(path, response, RESPONSE_SIZE)
                print(f"stream {stream_id} {ordering}", flush=True)
                paths.append(path)
        paths.extend(await run_fin_after_headers(client, authority, race_count))

        # Check the log while the connection is open: a transaction that never
        # finishes is torn down only when the connection closes.
        if access_log is not None:
            for path in await wait_for_access_log(access_log, paths):
                print(f"unlogged {path}", flush=True)
        await asyncio.sleep(LINGER_SECONDS)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument("--count", type=int, default=5, help="Requests to send for each sequential FIN ordering")
    parser.add_argument("--race-count", type=int, default=96, help="Requests whose FIN follows the headers by a few milliseconds")
    parser.add_argument("--access-log", type=Path, help="Access log that records each request path on its own line")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    asyncio.run(run(host, int(port_text), args.authority, args.server_name, args.count, args.race_count, args.access_log))


if __name__ == "__main__":
    main()
