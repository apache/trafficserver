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

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory

# The stall case completes once the 2 second server no-activity timeout fires.
# The 5 second client no-activity timeout fires first for an HttpSM stuck
# waiting on the transform, which over HTTP/2 logs an "unknown case" warning,
# and curl gives up after CLIENT_MAX_TIME seconds.
SERVER_NO_ACTIVITY_TIMEOUT = 2
CLIENT_NO_ACTIVITY_TIMEOUT = 5
CLIENT_MAX_TIME = 10


@pytest.mark.parametrize(
    ("protocol", "mode"),
    [
        ("http2", "close"),
        ("http2", "stall"),
        ("http1.1", "close"),
        ("http1.1", "stall"),
    ],
)
def test_compress_truncated_origin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, protocol: str, mode: str) -> None:
    """A truncated origin body during a compress.so transform yields a 502.

    The origin sends response headers but none of the body, then either closes
    the connection or stalls until the server no-activity timeout. The tunnel
    aborts the transform before it ever ran and signaled TRANSFORM_READ_READY,
    so the HttpSM must not keep waiting for it: the client never got response
    headers and, over HTTP/2, the stream would otherwise hang until the client
    gave up.

    :param ats_factory: Factory for isolated proxy instances.
    :param services: Factory owning the truncating origin.
    :param curl: Transport-aware client.
    :param protocol: Client protocol curl negotiates with ATS.
    :param mode: Whether the origin closes or stalls after the headers.
    """
    if protocol == "http2":
        if curl.uses_uds:
            pytest.skip("HTTP/2 requires a TLS listener")
        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process(
        "origin",
        (sys.executable, directory / "compress_truncated_origin.py", "--port", str(port)),
        ready_port=port,
    )
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=True)
    if not ats.plugin_exists("compress.so"):
        pytest.skip("compress.so is required")
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|compress|http_tunnel|http2_stream",
            "proxy.config.http.transaction_no_activity_timeout_out": SERVER_NO_ACTIVITY_TIMEOUT,
            "proxy.config.http.transaction_no_activity_timeout_in": CLIENT_NO_ACTIVITY_TIMEOUT,
            "proxy.config.http.connect_attempts_max_retries": 0,
        }
    )
    config = directory / "compress.config"
    ats.copy_to_config(config)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/ @plugin=compress.so @pparam={ats.config_directory / config.name}")
    origin.start()
    ats.start()

    if protocol == "http2":
        url = f"https://127.0.0.1:{ats.https_port}/{mode}/{protocol}/script.js"
    else:
        url = f"http://127.0.0.1:{ats.http_port}/{mode}/{protocol}/script.js"
    result = curl.run_for(
        ats,
        (
            f"--{protocol} --silent --insecure --output /dev/null --write-out '%{{http_code}}\\n' "
            f"--max-time {CLIENT_MAX_TIME} --header 'Accept-Encoding: gzip' {url}"
        ),
        timeout=CLIENT_MAX_TIME + 20,
    )
    assert "HTTP/2 unknown case" not in ats.diags_log.read_text(errors="replace")
    assert result.returncode == 0, result.output
    assert result.stdout.strip() == "502", result.output
    assert ats.is_running
