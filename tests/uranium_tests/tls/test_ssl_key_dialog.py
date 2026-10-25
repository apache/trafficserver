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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve the success response used before and after reload.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: bogus\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "success!"
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Install two encrypted keypairs and activate the first one.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "passphrase.pem",
        TEST_DIRECTORY / "ssl" / "passphrase.key",
        TEST_DIRECTORY / "ssl" / "passphrase2.pem",
        TEST_DIRECTORY / "ssl" / "passphrase2.key",
    )
    for hostname in ("passphrase", "passphrase2"):
        ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ http://127.0.0.1:{_origin.port}")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl_load|http",
    })
    ats.ssl_multicert_config.add_lines(multicert("passphrase"))
    return ats


def multicert(name: str) -> tuple[str, ...]:
    """Render an encrypted-key ssl_multicert entry.

    :param name: Unique service or case name within this test.
    """

    return (
        "ssl_multicert:",
        '  - dest_ip: "*"',
        f"    ssl_cert_name: {name}.pem",
        f"    ssl_key_name: {name}.key",
        '    ssl_key_dialog: "exec:/bin/bash -c \'echo -n passphrase\'"',
    )


def request(hostname: str, *, _ats: ATS, _curl: Curl) -> None:
    """Connect with SNI and validate the encrypted key's certificate.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--verbose --cacert '{str(TEST_DIRECTORY / 'ssl' / 'signer.pem')}' --resolve "
            f"'{hostname}:{_ats.https_port}:127.0.0.1' 'https://{hostname}:{_ats.https_port}/'"),
    )
    assert result.returncode == 0, result.output
    assert "200" in result.stderr
    assert result.stdout == "success!"


def test_ssl_key_dialog(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Encrypted TLS keys load and reload through an exec key dialog.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    request("passphrase", _ats=_ats, _curl=curl)
    _ats.ssl_multicert_config.path.write_text("\n".join(multicert("passphrase2")) + "\n")
    reload_result = _ats.traffic_ctl("config", "reload")
    assert reload_result.returncode == 0, reload_result.output
    time.sleep(1)
    request("passphrase2", _ats=_ats, _curl=curl)
