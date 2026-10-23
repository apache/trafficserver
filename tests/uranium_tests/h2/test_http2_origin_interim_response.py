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

import re
import shutil
import sys
from pathlib import Path

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory


@pytest.mark.parametrize("mode", ["single", "multi", "continue", "cont", "none", "endstream", "finalsplit"])
def test_http2_origin_interim_response(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, mode: str) -> None:
    """Forward the final response after valid h2 interims and reject malformed ones.

    :param ats_factory: Factory for isolated proxy instances.
    :param services: Factory owning the custom HTTP/2 origin.
    :param curl: Transport-aware client.
    :param mode: Origin framing case, including split final headers.
    """
    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    port = services.allocate_port()
    origin = services.process(
        "origin",
        [sys.executable, Path(__file__).with_name("h2_interim_origin.py"), "127.0.0.1",
         str(port), "--mode", mode],
        ready_port=port)
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map http://ats.test/{mode} https://127.0.0.1:{port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.http.server_session_sharing.pool": "thread",
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2|http",
        })
    origin.start()
    ats.start()
    result = curl.run_for(ats, f'--verbose --silent --max-time 10 -H "Host: ats.test" http://127.0.0.1:{ats.http_port}/{mode}')
    assert result.returncode == 0, result.output
    if mode == "endstream":
        assert re.search(r"HTTP/.* 5[0-9][0-9]", result.output), result.output
        assert "interim-origin-body" not in result.output
    else:
        assert re.search(r"HTTP/.* 200", result.output), result.output
        assert "interim-origin-body" in result.output
    assert origin.is_running
