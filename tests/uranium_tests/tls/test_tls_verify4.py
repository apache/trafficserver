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

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create an HTTPS origin whose certificate is not trusted by ATS.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: random.example\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure enforced verification against an unrelated CA.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.copy_to_ssl(SSL_DIRECTORY / "signer.pem")
    ats.remap_config.add_line(f"map / https://127.0.0.1:{_origin.https_port}")
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
            "proxy.config.ssl.client.verify.server.properties": "ALL",
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl",
        })
    return ats


def request(host: str, *, _ats: ATS, _curl: Curl) -> str:
    """Send one request with @a host through the ATS TLS listener.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    """

    result = _curl.run_for(
        _ats,
        f"--insecure --header 'Host: {host}' 'https://127.0.0.1:{_ats.https_port}/'",
    )
    assert result.returncode == 0, result.output
    return result.stdout


def set_policy(policy: str, *, _ats: ATS) -> None:
    """Set the reloadable outbound verification policy.

    :param _ats: Test-local ats configured by the test.
    :param policy: Policy used by this test step.
    """

    result = _ats.traffic_ctl("config", "set", "proxy.config.ssl.client.verify.server.policy", policy)
    assert result.returncode == 0, result.output
    time.sleep(0.2)


def test_tls_verify4(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Outbound TLS verification policy changes take effect without restart.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    assert "Could Not Connect" in request("random2.com", _ats=_ats, _curl=curl)
    set_policy("PERMISSIVE", _ats=_ats)
    assert "Could Not Connect" not in request("random3.com", _ats=_ats, _curl=curl)
    set_policy("ENFORCED", _ats=_ats)
    assert "Could Not Connect" in request("random4.com", _ats=_ats, _curl=curl)

    diagnostics = _ats.diags_log.read_text(errors="replace")
    assert "Core server certificate verification failed for (random3.com). Action=Continue" in diagnostics
    assert "Core server certificate verification failed for (random2.com). Action=Terminate" in diagnostics
