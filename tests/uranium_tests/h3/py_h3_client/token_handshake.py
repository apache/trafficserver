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
"""Complete a QUIC handshake with ATS while offering a token it did not issue."""

from __future__ import annotations

import argparse
import asyncio
import ssl

from aioquic.asyncio.client import connect
from aioquic.h3.connection import H3_ALPN
from aioquic.quic.configuration import QuicConfiguration

# Laid out like a retry token whose original DCID is 4 bytes long, but with a
# MAC ATS never computed, so the server has no reason to trust any of it.
UNISSUED_TOKEN = bytes([1]) + bytes(32) + bytes([4]) + b"odci" + bytes([0])


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN, server_name=args.server_name, token=UNISSUED_TOKEN)
    configuration.verify_mode = ssl.CERT_NONE

    async with connect(host, int(port_text), configuration=configuration, wait_connected=True) as client:
        await client.ping()
    print("handshake completed with an unissued token")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
