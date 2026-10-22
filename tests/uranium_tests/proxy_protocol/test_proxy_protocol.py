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
"""Verify incoming Proxy Protocol v1 on TCP and TLS listeners."""
import json
import subprocess
from pathlib import Path

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold


def test_proxy_protocol(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Forward the Proxy Protocol address and scheme to the HTTP origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the HTTP origin.
    :param curl: Transport-aware curl runner.
    """
    help_output = subprocess.run(("curl", "--help", "all"), capture_output=True, text=True, check=True).stdout
    if "--haproxy-protocol" not in help_output:
        pytest.skip("curl --haproxy-protocol is required")
    directory = Path(__file__).parent
    origin = services.httpbin("httpbin")
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    ats.records.update(
        {
            "proxy.config.http.server_ports": f"{ats.http_port}:pp {ats.https_port}:ssl:pp",
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
            "proxy.config.http.insert_forwarded": "for|proto",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol",
        })
    origin.start()
    ats.start()
    for index, scheme, port in ((0, "http", ats.http_port), (1, "https", ats.https_port)):
        result = curl.run_for(ats, f"--verbose --silent --insecure --haproxy-protocol --http1.1 {scheme}://localhost:{port}/get")
        assert result.returncode == 0, result.output
        body = json.dumps(json.loads(result.stdout), indent=2, separators=(",", ": ")) + "\n"
        assert_matches_gold(body, directory / "gold" / f"test_case_{index}_stdout.gold")
        assert_matches_gold(result.stderr, directory / "gold" / f"test_case_{index}_stderr.gold")
