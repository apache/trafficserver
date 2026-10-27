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
"""Validate S3 configuration parsing and all generated signature headers."""
from pathlib import Path
from tools.uranium.services import ATS, Curl, ServiceFactory, assert_matches_gold


def test_s3_auth_config(ats: ATS, services: ServiceFactory, curl: Curl) -> None:
    """Parse the long credential and validate signing diagnostics.

    :param ats: Isolated proxy.
    :param services: Origin factory.
    :param curl: Curl runner preserving the signed header set.
    """
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /s3-bucket HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "success!"
        })
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "s3_auth"})
    ats.copy_to_config("rules/v4-parse-test.test_input")
    ats.remap_config.add_line(
        f"map http://www.example.com http://127.0.0.1:{origin.port} @plugin=s3_auth.so "
        f"@pparam=--config @pparam={ats.config_directory}/v4-parse-test.test_input")
    origin.start()
    ats.start()
    result = curl.get(ats, "/s3-bucket", headers={"Host": "www.example.com"}, options="-s -v")
    assert result.returncode == 0, result.output
    assert "200 OK" in result.stderr, result.output
    assert "Content-Length: 8" in result.stderr, result.output
    ats.stop()
    assert_matches_gold(ats.traffic_out.read_text(errors="replace"), Path(__file__).parent / "gold/s3_auth_parsing_ts.gold")
