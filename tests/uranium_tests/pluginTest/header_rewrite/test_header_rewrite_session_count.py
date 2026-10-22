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
"""Keep the same origin session across independently connected clients."""
from pathlib import Path
from tools.uranium.services import ATS, Curl, ServiceFactory, assert_matches_gold


def test_header_rewrite_cond_ssn_txn_count(ats: ATS, services: ServiceFactory, curl: Curl) -> None:
    """Close the origin session only after its transaction threshold.

    :param ats: Isolated proxy.
    :param services: Origin factory.
    :param curl: Independent curl client runner.
    """
    origin = services.origin("origin")
    for path, connection in (("hello", ""), ("world", "Connection: close\r\n")):
        origin.add_response(
            {"headers": f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n"},
            {"headers": f"HTTP/1.1 200 OK\r\nServer: microserver\r\n{connection}Content-Length: 0\r\n\r\n"})
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "header.*",
            "proxy.config.http.auth_server_session_private": 1,
            "proxy.config.http.server_session_sharing.pool": "global",
            "proxy.config.http.server_session_sharing.match": "both",
        })
    ats.copy_to_config("rules/rule_set_header_after_ssn_txn_count.conf")
    ats.remap_config.add_line(
        f"map / http://127.0.0.1:{origin.port} @plugin=header_rewrite.so "
        f"@pparam={ats.config_directory}/rule_set_header_after_ssn_txn_count.conf")
    origin.start()
    ats.start()
    output = ""
    for path in ("hello", "hello", "hello", "hello", "world"):
        connection = "close" if path == "world" else "keep-alive"
        result = curl.get(ats, f"/{path}", headers={"Host": "www.example.com", "Connection": connection}, options="-v")
        assert result.returncode == 0, result.output
        output += result.stderr
    assert_matches_gold(output, Path(__file__).parent / "gold/header_rewrite_cond_ssn_txn_count.gold")
