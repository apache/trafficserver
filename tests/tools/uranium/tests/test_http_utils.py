# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from unittest.mock import Mock

import pytest

from tools.http_utils import drain_socket


@pytest.mark.parametrize(
    "remaining, received, chunks", [
        (0, b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", []),
        (0, b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", []),
        (3, b"", [b"a", b"bc"]),
        (-1, b"", [b"3\r\nabc\r\n", b"0\r\n\r\n"]),
    ])
def test_drain_stops_at_message_end(remaining: int, received: bytes, chunks: list[bytes]) -> None:
    """A completed response must not wait for a persistent socket to close.

    :param remaining: Outstanding body bytes, or -1 for chunked encoding.
    :param received: Response bytes already consumed by the caller.
    :param chunks: Socket reads needed to complete the body.
    """
    sock = Mock()
    sock.recv.side_effect = chunks
    drain_socket(sock, received, remaining)
    assert sock.recv.call_count == len(chunks)
