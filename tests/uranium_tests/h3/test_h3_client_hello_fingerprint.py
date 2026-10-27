#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import re
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines, wait_for_metric

PATHS = ("/h3", "/h1")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve an empty response for each request path.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    for path in PATHS:
        origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
    return origin


def require_h3(ats_factory: ATSFactory, curl: Curl) -> None:
    """Skip unless ATS uses OpenSSL's native QUIC, whose ClientHello hooks these tests cover, and curl supports HTTP/3.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    curl_help = subprocess.check_output(("curl", "--help", "all"), text=True)
    if not ats_factory.has_feature("TS_USE_QUIC") or not Curl.supports("http3") or "--http3-only" not in curl_help:
        pytest.skip("ATS QUIC and curl HTTP/3 support are required")
    if not ats_factory.has_feature("TS_HAS_OPENSSL_QUIC"):
        pytest.skip("ATS must use OpenSSL's native QUIC")
    if curl.uses_uds:
        pytest.skip("HTTP/3 requires a UDP listener")


def create_ats(ats_factory: ATSFactory, *, origin: OriginServer, debug_tags: str, plugins: tuple[str, ...]) -> ATS:
    """Create a QUIC-enabled ATS that forwards to the origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param origin: Origin that ATS forwards requests to.
    :param debug_tags: Debug tags to enable.
    :param plugins: Plugins that must be built for the test to run.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_quic=True, enable_cache=False)
    for plugin in plugins:
        if not ats.plugin_exists(plugin):
            pytest.skip(f"{plugin} is required")
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            "  - ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
            '    dest_ip: "*"',
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": debug_tags,
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    return ats


def request(ats: ATS, curl: Curl, *, protocol: str, path: str) -> None:
    """Send one request through ATS and require it to succeed.

    :param ats: Traffic Server instance to send the request to.
    :param curl: Transport-aware curl command runner.
    :param protocol: The curl option selecting the HTTP version.
    :param path: The request path.
    """
    result = curl.run_for(
        ats,
        (
            f"--silent --show-error --fail --ipv4 --insecure {protocol} "
            f"--resolve 'foo.com:{ats.https_port}:127.0.0.1' --output /dev/null "
            f"'https://foo.com:{ats.https_port}{path}'"),
        timeout=30,
    )
    assert result.returncode == 0, result.output


def test_h3_client_hello_fingerprint(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The ClientHello fingerprinting plugins fingerprint HTTP/3 as well as HTTP/1.1 over TLS connections.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    require_h3(ats_factory, curl)
    origin = configure_origin(services)
    ats = create_ats(
        ats_factory,
        origin=origin,
        debug_tags="jax_fingerprint|ja3_fingerprint|test",
        plugins=("jax_fingerprint.so", "ja3_fingerprint.so"))
    ats.plugin_config.add_lines(
        (
            "jax_fingerprint.so --method JA4 --standalone --log-filename jax_fingerprint",
            "ja3_fingerprint.so --ja3log",
        ))
    # A global TS_HTTP_SSN_START_HOOK, whose callout used to crash on HTTP/3 sessions.
    ats.copy_custom_plugin("{AtsTestPluginsDir}/hook_add_plugin.so")
    ats.plugin_config.add_line("hook_add_plugin.so")

    origin.start()
    ats.start()
    request(ats, curl, protocol="--http3-only", path="/h3")
    request(ats, curl, protocol="--http1.1", path="/h1")

    # JA4 starts with the transport: q for QUIC, t for TLS over TCP.
    ja4_log = wait_for_file_lines(ats.log_directory / "jax_fingerprint.log", r"JA4: ", 2)
    assert re.search(r"JA4: q13d[0-9]{4}h3_[0-9a-f]{12}_[0-9a-f]{12}$", ja4_log, re.MULTILINE), ja4_log
    assert re.search(r"JA4: t13d[0-9]{4}h1_[0-9a-f]{12}_[0-9a-f]{12}$", ja4_log, re.MULTILINE), ja4_log

    ja3_log = wait_for_file_lines(ats.log_directory / "ja3_fingerprint.log", r"JA3: ", 2)
    assert len(re.findall(r"JA3: 771,[0-9-]+,[0-9-]+,[0-9-]+,[0-9-]*\tMD5: [0-9a-f]{32}$", ja3_log, re.MULTILINE)) == 2, ja3_log

    traffic_out = ats.traffic_out.read_text(errors="replace")
    assert len(re.findall(r"globalHandler :: TS_EVENT_HTTP_SSN_START", traffic_out)) == 2, \
        "both sessions should run the TS_HTTP_SSN_START_HOOK"


def test_h3_delayed_client_hello_hook(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An HTTP/3 ClientHello hook holds the handshake until it reenables, and later hooks still read the ClientHello.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    require_h3(ats_factory, curl)
    origin = configure_origin(services)
    ats = create_ats(
        ats_factory, origin=origin, debug_tags="quic_net|jax_fingerprint|ssl_hook_test", plugins=("jax_fingerprint.so",))
    # A ClientHello hook that reenables the connection two seconds later, ahead of jax_fingerprint's.
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
    ats.plugin_config.add_line("ssl_hook_test.so -client_hello=1")
    ats.plugin_config.add_line("jax_fingerprint.so --method JA4 --standalone --log-filename jax_fingerprint")

    origin.start()
    ats.start()
    request(ats, curl, protocol="--http3-only", path="/h3")

    ja4_log = wait_for_file_lines(ats.log_directory / "jax_fingerprint.log", r"JA4: ", 1)
    assert re.search(r"JA4: q13d[0-9]{4}h3_[0-9a-f]{12}_[0-9a-f]{12}$", ja4_log, re.MULTILINE), ja4_log

    traffic_out = ats.traffic_out.read_text(errors="replace")
    hook_calls = re.findall(r"Client Hello callback [0-9]+ ssl_vc=\S+ - event is (\w+)", traffic_out)
    assert hook_calls == ["good"], hook_calls
    # As over TCP, the ClientHello hooks run, and can hold the handshake, before it completes.
    reenabled = traffic_out.index("Callback reenable ssl_vc=")
    fingerprinted = traffic_out.index("Fingerprint: q13d")
    established = traffic_out.index("Enter state_connection_established")
    assert reenabled < fingerprinted < established, "the HTTP/3 handshake should wait for the ClientHello hooks"


def test_h3_client_hello_hook_rejection(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A ClientHello hook that reenables an HTTP/3 connection with TS_EVENT_ERROR fails its handshake.

    abuse_shield rejects a JA3 denylisted ClientHello this way. The client is allowed until its own JA3 is
    denylisted.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    require_h3(ats_factory, curl)
    origin = configure_origin(services)
    ats = create_ats(
        ats_factory, origin=origin, debug_tags="abuse_shield|jax_fingerprint", plugins=("jax_fingerprint.so", "abuse_shield.so"))
    config = (
        "global:\n  ip_tracking: {slots: 1000}\n  log_interval_sec: 0\n"
        "  fingerprint_registry: abuse_shield.fingerprints\n"
        "rules:\n  - name: blocked_ja3\n    filter:\n      fingerprints:\n        JA3:\n"
        '          - "JA3_HASH"\n    action: [log, close]\nenabled: true\n')
    ats.write_config_file("abuse_shield.yaml", config.replace("JA3_HASH", "0" * 32))
    # abuse_shield logs the rejection it is configured to make as an error.
    ats.allow_errors(r'\[abuse_shield\] Rule "blocked_ja3" matched')
    ats.plugin_config.add_lines(
        (
            "jax_fingerprint.so --method JA3 --export abuse_shield.fingerprints",
            "abuse_shield.so abuse_shield.yaml",
        ))

    origin.start()
    ats.start()
    request(ats, curl, protocol="--http3-only", path="/h3")
    traffic_out = wait_for_file_lines(ats.traffic_out, r"\(jax_fingerprint\) Fingerprint: [0-9a-f]{32}$", 1)
    ja3 = re.search(r"\(jax_fingerprint\) Fingerprint: ([0-9a-f]{32})$", traffic_out, re.MULTILINE).group(1)

    (ats.config_directory / "abuse_shield.yaml").write_text(config.replace("JA3_HASH", ja3))
    reload_result = ats.traffic_ctl("plugin", "msg", "abuse_shield.reload")
    assert reload_result.returncode == 0, reload_result.output
    wait_for_file_lines(ats.diags_log, "Configuration reloaded successfully", 1)

    result = curl.run_for(
        ats,
        (
            "--silent --show-error --fail --ipv4 --insecure --http3-only "
            f"--resolve 'foo.com:{ats.https_port}:127.0.0.1' --output /dev/null "
            f"'https://foo.com:{ats.https_port}/h3'"),
        timeout=30,
    )
    assert result.returncode != 0, result.output
    wait_for_file_lines(ats.diags_log, rf'Rule "blocked_ja3" matched for IP=.*fingerprint=JA3:{ja3}.*actions=\[log,close\]', 1)
    assert wait_for_metric(ats, "abuse_shield.fingerprints.rejected", 1) == 1
