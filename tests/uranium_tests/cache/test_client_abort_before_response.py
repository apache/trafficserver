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
"""Verify origin handling when a client aborts before the response."""

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
ORIGIN_DELAY_SECONDS = 6
CLIENT_TIMEOUT_SECONDS = 2
ORIGIN_WAIT_SECONDS = ORIGIN_DELAY_SECONDS + 3
ABORT_DETECTED = "proxy_closed_connection"
ABORT_NOT_DETECTED = "proxy_kept_connection_open"


class ClientAbortBeforeResponseScenario:
    """Check whether ATS closes a delayed origin after its client leaves."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        enable_tls: bool,
        use_http2: bool,
        allow_half_open: int,
        expect_abort: bool,
    ) -> None:
        """Configure one client protocol and half-open policy.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param services: Factory for the delayed origin process.
        :param curl: Transport-aware curl command runner.
        :param enable_tls: Whether the client talks to ATS over TLS.
        :param use_http2: Whether curl uses HTTP/2 rather than HTTP/1.1.
        :param allow_half_open: Value for ``proxy.config.http.allow_half_open``.
        :param expect_abort: Whether ATS should close the origin connection.
        """

        self._curl = curl
        self._enable_tls = enable_tls
        self._use_http2 = use_http2
        origin_port = services.allocate_port()
        self._origin = self.configure_origin(services, origin_port)
        self._ats = self.configure_ats(ats_factory, origin_port, allow_half_open)
        self._expect_abort = expect_abort

    @staticmethod
    def configure_origin(services: ServiceFactory, port: int) -> ProcessService:
        """Create the helper that observes whether ATS closes its connection.

        :param services: Factory for bespoke support processes.
        :param port: TCP port on which the origin listens.
        """

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "abort_detecting_origin.py", str(port), "--delay", str(ORIGIN_DELAY_SECONDS)),
            ready_port=port,
        )

    def configure_ats(self, ats_factory: ATSFactory, origin_port: int, allow_half_open: int) -> ATS:
        """Configure ATS with the selected client and origin behavior.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param origin_port: Port of the abort-detecting origin.
        :param allow_half_open: Value for ``proxy.config.http.allow_half_open``.
        """

        ats = ats_factory.create("ts", enable_tls=self._enable_tls, enable_cache=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.allow_half_open": allow_half_open,
                "proxy.config.http.cache.required_headers": 0,
            })
        if self._enable_tls:
            ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{origin_port}/")
        return ats

    def run(self) -> None:
        """Abort curl, wait for the origin verdict, and validate it."""

        if self._curl.uses_uds and self._enable_tls:
            pytest.skip("TLS client-abort cases require a TCP listener")
        if self._use_http2 and not self._curl.supports("http2"):
            pytest.skip("curl HTTP/2 support is required")

        self._origin.start()
        self._ats.start()
        scheme = "https" if self._enable_tls else "http"
        port = self._ats.https_port if self._enable_tls else self._ats.http_port
        version = "--http2" if self._use_http2 else ""
        result = self._curl.run_script(
            self._ats,
            f"{{curl}} --silent --insecure --output /dev/null {version} --max-time {CLIENT_TIMEOUT_SECONDS} "
            f"{scheme}://127.0.0.1:{port}/slow; sleep {ORIGIN_WAIT_SECONDS}",
            timeout=ORIGIN_WAIT_SECONDS + 10,
        )
        assert result.returncode == 0, result.output
        origin = self._origin.wait(timeout=5)
        expected = ABORT_DETECTED if self._expect_abort else ABORT_NOT_DETECTED
        unexpected = ABORT_NOT_DETECTED if self._expect_abort else ABORT_DETECTED
        assert expected in origin.output, origin.output
        assert unexpected not in origin.output, origin.output


@pytest.mark.parametrize(
    ("enable_tls", "use_http2", "allow_half_open", "expect_abort"),
    (
        pytest.param(False, False, 0, True, id="http-half-open-disabled"),
        pytest.param(True, False, 0, True, id="https-half-open-disabled"),
        pytest.param(True, False, 1, False, id="https-half-open-enabled"),
        pytest.param(True, True, 0, True, id="h2-half-open-disabled"),
        pytest.param(True, True, 1, False, id="h2-half-open-enabled"),
    ),
)
def test_client_abort_before_response(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    enable_tls: bool,
    use_http2: bool,
    allow_half_open: int,
    expect_abort: bool,
) -> None:
    """ATS applies the half-open policy to each supported client protocol.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the delayed origin process.
    :param curl: Transport-aware curl command runner.
    :param enable_tls: Whether the client talks to ATS over TLS.
    :param use_http2: Whether curl uses HTTP/2 rather than HTTP/1.1.
    :param allow_half_open: Value for ``proxy.config.http.allow_half_open``.
    :param expect_abort: Whether ATS should close the origin connection.
    """

    ClientAbortBeforeResponseScenario(
        ats_factory,
        services,
        curl,
        enable_tls=enable_tls,
        use_http2=use_http2,
        allow_half_open=allow_half_open,
        expect_abort=expect_abort,
    ).run()
