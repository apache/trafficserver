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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create an HTTPS origin with a certificate that needs hook handling.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Load the verification plugin and configure three SNI policies.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
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
            "proxy.config.diags.debug.tags": "ssl_verify_test",
            "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
            "proxy.config.ssl.client.verify.server.properties": "NONE",
            "proxy.config.url_remap.pristine_host_hdr": 1,
        }
    )
    for hostname in ("foo.com", "bar.com", "random.com"):
        ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ https://127.0.0.1:{_origin.https_port}")
    ats.write_config_file("sni.yaml", "sni:\n- fqdn: bar.com\n  verify_server_policy: PERMISSIVE\n")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_verify_test.so")
    ats.plugin_config.add_line("ssl_verify_test.so -count=2 -bad=random.com -bad=bar.com")
    return ats


def request(hostname: str, *, _ats: ATS, _curl: Curl) -> str:
    """Send one TLS request using @a hostname as SNI and Host.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    """

    result = _curl.run_for(
        _ats,
        (f"--resolve '{hostname}:{_ats.https_port}:127.0.0.1' --insecure 'https://{hostname}:{_ats.https_port}'"),
    )
    assert result.returncode == 0, result.output
    return result.output


def test_tls_hooks_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SERVER_VERIFY_HOOK decisions honor the configured SNI policy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    assert "Could Not Connect" not in request("foo.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" in request("random.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" not in request("bar.com", _ats=_ats, _curl=curl)

    diags = _ats.diags_log.read_text(errors="replace")
    assert "Action=Terminate SNI=random.com" in diags
    assert "Action=Continue SNI=bar.com" in diags
    assert "SNI=foo.com" not in diags

    traffic_out = _ats.traffic_out.read_text(errors="replace")
    for hostname, outcome in (("foo.com", "good HS"), ("random.com", "error HS"), ("bar.com", "error HS")):
        for callback in (0, 1):
            expression = rf"Server verify callback {callback} [\da-fx]+? - event is good SNI={hostname} {outcome}"
            assert re.search(expression, traffic_out), traffic_out
    assert "Server verify callback SNI APIs match=true" in traffic_out


@pytest.mark.parametrize("origin_alpn", ("http/1.1", "h2,http/1.1"), ids=("http1", "h2"))
@pytest.mark.parametrize("tls_version", ("1.3", "1.2"))
@pytest.mark.parametrize("policy", ("PERMISSIVE", "ENFORCED"))
def test_tls_hooks_verify_rejection_with_tls_origin(
    ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, policy: str, tls_version: str, origin_alpn: str
) -> None:
    """A SERVER_VERIFY_HOOK rejection fails the origin handshake under ENFORCED, and under PERMISSIVE lets it complete.

    The origin is another Traffic Server, which also accepts HTTP/2. When HTTP/2 is offered, the request
    reaches it only if the negotiated protocol was recorded when the handshake completed. ssl_verify_test
    rejects every connection that sends no SNI, as one toward an IP literal does.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param policy: proxy.config.ssl.client.verify.server.policy toward the origin.
    :param tls_version: The TLS version the hop to the origin is held to.
    :param origin_alpn: Protocols offered to the origin.
    """
    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "origin response"},
    )

    tls_origin = ats_factory.create("tls_origin", enable_tls=True, enable_cache=False)
    tls_origin.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    tls_origin.set_ssl_multicert_yaml(
        {"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]}
    )
    tls_origin.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]})
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify_test",
            "proxy.config.ssl.client.verify.server.policy": policy,
            "proxy.config.ssl.client.verify.server.properties": "NONE",
            "proxy.config.ssl.client.version.max": 2 if tls_version == "1.2" else 3,
            "proxy.config.ssl.client.alpn_protocols": origin_alpn,
            # A retried connection would otherwise resume the session the first one cached, and a
            # resumed handshake runs no verify hook.
            "proxy.config.ssl.origin_session_cache.enabled": 0,
        }
    )
    ats.remap_config.add_line(f"map / https://127.0.0.1:{tls_origin.https_port}")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_verify_test.so")
    ats.plugin_config.add_line("ssl_verify_test.so -count=1")

    origin.start()
    tls_origin.start()
    ats.start()
    result = curl.run(f"-k https://127.0.0.1:{ats.https_port}/", timeout=30)
    h2_metric = ats.traffic_ctl("metric", "get", "proxy.process.http2.total_server_connections")
    assert h2_metric.returncode == 0, h2_metric.output
    ats.stop()

    assert result.returncode == 0, result.output
    action = "Continue" if policy == "PERMISSIVE" else "Terminate"
    assert f"plugin failed the origin certificate check for 127.0.0.1.  Action={action}" in ats.diags_log.read_text(
        errors="replace"
    ), "the hook must have rejected the origin"
    if policy == "PERMISSIVE":
        assert "origin response" in result.output, "the request should be served over the handshake PERMISSIVE continued"
        if origin_alpn.startswith("h2"):
            assert int(h2_metric.stdout.split()[-1]) > 0, "the request should go to the origin over the HTTP/2 it negotiated"
    else:
        assert "origin response" not in result.output, "ENFORCED must refuse the origin the hook rejected"
