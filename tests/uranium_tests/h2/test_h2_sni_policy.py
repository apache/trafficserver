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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Configure the shared empty origin response.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, accept_threads: int, *, _origin: OriginServer, _sni_enables_h2: bool) -> ATS:
    """Configure the global protocol and opposing SNI policy.

    :param _origin: Test-local origin configured by the test.
    :param _sni_enables_h2: Test-local sni enables h2 configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param accept_threads: Accept threads used by this test step.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http|ssl",
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.accept_threads": accept_threads,
        })
    if _sni_enables_h2:
        ats.records.update({
            "proxy.config.http.server_ports": f"{ats.https_port}:ssl:proto=http {ats.http_port}",
        })
    state = "on" if _sni_enables_h2 else "off"
    ats.write_config_file("sni.yaml", f'''sni:
- fqdn: bar.com
  http2: {state}
- fqdn: "*.foo.com"
  http2: {state}
''')
    return ats


def request(hostname: str, expects_h2: bool, *, _ats: ATS, _curl: Curl) -> None:
    """Connect with one SNI name and verify the negotiated protocol.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    :param expects_h2: Expects h2 used by this test step.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--verbose --insecure --ipv4 --resolve '{hostname}:{_ats.https_port}:127.0.0.1' "
            f"'https://{hostname}:{_ats.https_port}/'"),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "Could Not Connect" not in result.output
    negotiated_h2 = re.search(r"using HTTP/?2", result.output, re.IGNORECASE) is not None
    assert negotiated_h2 is expects_h2, result.output


@pytest.mark.parametrize("accept_threads", [0, 1], ids=["net-thread-accept", "dedicated-accept-thread"])
@pytest.mark.parametrize("sni_enables_h2", [False, True], ids=["sni-disables-h2", "sni-enables-h2"])
def test_h2_sni_policy(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    sni_enables_h2: bool,
    accept_threads: int,
) -> None:
    """SNI HTTP/2 policy works with either listener acceptance model.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param sni_enables_h2: Sni enables h2 used by this test step.
    :param accept_threads: Accept threads used by this test step.
    """
    if not Curl.supports("http2"):
        pytest.skip("curl lacks HTTP/2 support")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, accept_threads, _origin=_origin, _sni_enables_h2=sni_enables_h2)

    _origin.start()
    _ats.start()
    request("foo.com", not sni_enables_h2, _ats=_ats, _curl=curl)
    request("bar.com", sni_enables_h2, _ats=_ats, _curl=curl)
    request("bob.foo.com", sni_enables_h2, _ats=_ats, _curl=curl)
