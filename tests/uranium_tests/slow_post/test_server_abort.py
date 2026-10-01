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


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Start an ordinary HTTP microserver on the mapped HTTPS port.

    :param services: Factory owning support services and their cleanup.
    """

    return services.origin("origin")


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Deliberately map to the clear-text origin using an HTTPS URL.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.remap_config.add_line(f"map / https://127.0.0.1:{_origin.port}")
    ats.ssl_multicert_config.add_lines(
        [
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: aaa-signed.pem",
            "    ssl_key_name: aaa-signed.key",
        ])
    secrets = TEST_DIRECTORY / "test_secrets"
    ats.copy_to_ssl(secrets / "aaa-signed.pem", secrets / "aaa-signed.key")
    ats.records.update(
        {
            "proxy.config.diags.debug.tags": "http|dns",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.http.connect_attempts_max_retries": 0,
            "proxy.config.http.connect_attempts_rr_retries": 0,
            "proxy.config.http.connect_attempts_timeout": 2,
            "proxy.config.http.transaction_no_activity_timeout_out": 2,
        })
    return ats


def test_server_abort(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A non-TLS origin abort during handshake is handled without crashing ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    if curl.uses_uds:
        pytest.skip("the TLS client requires a TCP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.run(
        (f"--verbose --insecure --http1.1 --max-time 10 --header 'Host: foo.com' "
         f"'https://127.0.0.1:{_ats.https_port}/'"),
        timeout=15,
    )
    assert result.returncode in (0, 28), result.output
    deadline = time.monotonic() + 5
    origin_error = _origin.stderr_text
    while not re.search(r"UnicodeDecodeError|IndexError: list index out of range", origin_error):
        if time.monotonic() >= deadline:
            break
        time.sleep(0.05)
        origin_error = _origin.stderr_text
    assert re.search(r"UnicodeDecodeError|IndexError: list index out of range", origin_error), result.output + _origin.output
