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
import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class OriginAbortScenario:
    """Make ATS perform a TLS handshake with a clear-text microserver."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Start an ordinary HTTP microserver on the mapped HTTPS port."""

        return services.origin("origin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Deliberately map to the clear-text origin using an HTTPS URL."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._origin.port}")
        ats.ssl_multicert_config.add_lines(
            [
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: aaa-signed.pem",
                "    ssl_key_name: aaa-signed.key",
            ])
        secrets = TEST_DIRECTORY / "test_secrets"
        ats.copy_to_ssl(secrets / "aaa-signed.pem", secrets / "aaa-signed.key")
        ats.records.update({
            "proxy.config.diags.debug.tags": "http|dns",
            "proxy.config.diags.debug.enabled": 1,
        })
        return ats

    def run(self) -> None:
        """Issue the request and verify the clear-text origin aborts parsing TLS."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run(
            (
                f"--verbose --insecure --http1.1 --max-time 10 --header 'Host: foo.com' "
                f"'https://127.0.0.1:{self._ats.https_port}/'"),
            timeout=15,
        )
        assert result.returncode in (0, 28), result.output
        deadline = time.monotonic() + 5
        origin_error = self._origin.stderr
        while not re.search(r"UnicodeDecodeError|IndexError: list index out of range", origin_error):
            if time.monotonic() >= deadline:
                break
            time.sleep(0.05)
            origin_error = self._origin.stderr
        assert re.search(
            r"UnicodeDecodeError|IndexError: list index out of range", origin_error), result.output + self._origin.output


def test_server_abort(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A non-TLS origin abort during handshake is handled without crashing ATS."""

    if curl.uses_uds:
        pytest.skip("the TLS client requires a TCP listener")
    OriginAbortScenario(ats_factory, services, curl).run()
