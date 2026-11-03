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

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TOOLS_DIRECTORY = Path(__file__).parents[2] / "tools"
BODY = "close-delimited body"


def configure_origin(services: ServiceFactory, *, _port: int) -> ProcessService:
    """Serve a close-delimited TLS response and close without close_notify.

    :param services: Factory owning support services and their cleanup.
    :param _port: Port the origin listens on.
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
            "--close-delimited",
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


def test_tls_origin_close_without_notify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A close-delimited TLS origin response that ends without close_notify reaches the client whole.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _port = services.allocate_port()
    _origin = configure_origin(services, _port=_port)
    _ats = configure_ats(ats_factory, _port=_port)

    _origin.start()
    _ats.start()
    result = curl.run_for(_ats, f"--silent --show-error --max-time 10 http://127.0.0.1:{_ats.http_port}/")
    assert result.returncode == 0, result.output
    assert result.stdout == BODY
    _origin.wait(timeout=5)
