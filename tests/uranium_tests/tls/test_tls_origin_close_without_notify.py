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

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TOOLS_DIRECTORY = Path(__file__).parents[2] / "tools"
BODY = "origin response body"
CURLE_PARTIAL_FILE = 18
CURLE_GOT_NOTHING = 52


def configure_origin(services: ServiceFactory, *, _port: int, _framing: tuple[str, ...]) -> ProcessService:
    """Serve one TLS response and close without close_notify.

    :param services: Factory owning support services and their cleanup.
    :param _port: Port the origin listens on.
    :param _framing: mock_origin.py options that frame or truncate the response.
    """

    return services.process(
        "origin",
        (
            sys.executable,
            TOOLS_DIRECTORY / "mock_origin.py",
            str(_port),
            "--tls",
            TOOLS_DIRECTORY / "ssl" / "server.pem",
            TOOLS_DIRECTORY / "ssl" / "server.key",
            "--header",
            "Connection: close",
            "--body",
            BODY,
            *_framing,
        ),
        ready_port=_port,
    )


def configure_ats(ats_factory: ATSFactory, *, _port: int) -> ATS:
    """Proxy plain HTTP clients to the TLS origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _port: Port the origin listens on.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.remap_config.add_line(f"map / https://127.0.0.1:{_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "DISABLED",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl|http_tunnel",
        })
    return ats


@pytest.mark.parametrize(
    ("framing", "complete"),
    [
        pytest.param(("--close-delimited",), True, id="close-delimited"),
        pytest.param(("--truncate", "5"), False, id="truncated-content-length"),
        pytest.param(("--chunked", "--truncate", "5"), False, id="truncated-chunked"),
    ],
)
def test_tls_origin_close_without_notify(
        framing: tuple[str, ...], complete: bool, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An origin close without close_notify completes a close-delimited body and fails a truncated one promptly.

    :param framing: mock_origin.py options that frame or truncate the response.
    :param complete: Whether the response's framing is satisfied when the origin closes.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _port = services.allocate_port()
    _origin = configure_origin(services, _port=_port, _framing=framing)
    _ats = configure_ats(ats_factory, _port=_port)

    _origin.start()
    _ats.start()
    result = curl.run_for(_ats, f"--silent --show-error --max-time 10 http://127.0.0.1:{_ats.http_port}/")
    if complete:
        assert result.returncode == 0, result.output
        assert result.stdout == BODY
    else:
        # ATS sends nothing when it sees the truncation before writing the header, else a partial body.
        assert result.returncode in (CURLE_GOT_NOTHING, CURLE_PARTIAL_FILE), result.output
    _origin.wait(timeout=5)
