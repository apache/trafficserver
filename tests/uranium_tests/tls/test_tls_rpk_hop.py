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

import os
import re
from collections.abc import Sequence
from pathlib import Path

import pytest

from tools.uranium.services import (
    ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines, wait_for_metric)

from ..jsonrpc.config_reload_helpers import reload_task_statuses

SSL_DIRECTORY = Path(__file__).parent / "ssl"

# The single-entry ssl_multicert.yaml both tiers serve their inbound TLS from. server.pubkey.pem is
# this certificate's public key, so it is what the pinning scenarios pin.
DEFAULT_MULTICERT_ENTRY = {"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}

# A different trusted set that still contains server.pem's key, so a connection pinned against it
# succeeds and only the set's identity changes.
WIDENED_PIN_SET = (SSL_DIRECTORY / "server.pubkey.pem").read_text() + (SSL_DIRECTORY / "server.wrongpubkey.pem").read_text()

NEW_ORIGIN_SESSION = "new session to origin server"
PIN_MISMATCH = r"Origin authenticated with a raw public key \(RFC 7250\), pin match=no"
REUSED_ORIGIN_SESSION = "reused session to origin server"


@pytest.fixture(autouse=True)
def require_rpk(ats_factory: ATSFactory) -> None:
    """Skip where RPK is not compiled in, which depends on the linked TLS library.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    if not ats_factory.has_feature("TS_USE_RPK"):
        pytest.skip("ATS was built without raw public key support")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the clear-text origin behind the parent tier.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "origin response"
        },
    )
    return origin


def configure_parent(
        ats_factory: ATSFactory,
        name: str,
        origin: OriginServer,
        *,
        rpk_enabled: bool,
        client_rpk_ca_file: str | None = None,
        client_cert_level: int = 0,
        client_ca_file: str | None = None,
        sni_client_ca_file: str | None = None,
        server_cert: str = "server.pem",
        server_key: str = "server.key",
        plugin: str | None = None) -> ATS:
    """Configure an upstream (parent) ATS that terminates TLS from the edge and forwards to the origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique ATS name within the test.
    :param origin: Origin the parent forwards to.
    :param rpk_enabled: Whether the parent offers its raw public key alongside X.509.
    :param client_rpk_ca_file: File of trusted client raw public keys (ssl_client_rpk_ca_name) that
        pins the edge's key for mTLS, or None for no pinning.
    :param client_cert_level: proxy.config.ssl.client.certification_level; 2 requires a client
        certificate and 0 leaves the record unset.
    :param client_ca_file: CA bundle that X.509 client certificates are verified against.
    :param sni_client_ca_file: CA bundle carried by a sni.yaml verify_client action as a
        per-connection override, which is what installs a per-connection verify store.
    :param server_cert: Certificate the parent serves, and whose key it offers as a raw public key.
    :param server_key: Private key for server_cert.
    :param plugin: plugin.config line for a test plugin to load, or None.
    """

    ats = ats_factory.create(name, enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(SSL_DIRECTORY / server_cert, SSL_DIRECTORY / server_key)
    load_test_plugin(ats, plugin)
    for filename in (client_rpk_ca_file, client_ca_file, sni_client_ca_file):
        if filename is not None:
            ats.copy_to_ssl(SSL_DIRECTORY / filename)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")

    entry: dict[str, object] = dict(DEFAULT_MULTICERT_ENTRY, ssl_cert_name=server_cert, ssl_key_name=server_key)
    if rpk_enabled:
        entry["ssl_rpk_enabled"] = 1
    if client_rpk_ca_file is not None:
        # Deliberately bare, not prefixed with the SSL directory: ssl_client_rpk_ca_name resolves
        # against proxy.config.ssl.CA.cert.path, matching the resolution ssl_ca_name already gets.
        entry["ssl_client_rpk_ca_name"] = client_rpk_ca_file
    ats.set_ssl_multicert_yaml({"ssl_multicert": [entry]})

    if sni_client_ca_file is not None:
        # Keyed on the name in server.pem, which is the SNI the client sends. An IP literal is never
        # sent as SNI, so an entry keyed on one could not match.
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "- fqdn: random.server.com\n"
            "  verify_client: STRICT\n"
            f"  verify_client_ca_certs: {ats.ssl_directory / sni_client_ca_file}\n",
        )

    records: dict[str, object] = {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl_verify|ssl_load",
    }
    if client_rpk_ca_file is not None or client_cert_level:
        records["proxy.config.ssl.CA.cert.path"] = str(ats.ssl_directory)
    if client_cert_level:
        records["proxy.config.ssl.client.certification_level"] = client_cert_level
    if client_ca_file is not None:
        records["proxy.config.ssl.CA.cert.filename"] = client_ca_file
    ats.records.update(records)
    return ats


def configure_edge(
        ats_factory: ATSFactory,
        name: str,
        parent: ATS,
        pin_file: str | None,
        *,
        policy: str = "ENFORCED",
        offer_client_rpk: bool = False,
        offer_client_x509: bool = False,
        client_cert: str = "server.pem",
        client_key: str = "server.key",
        server_ca_file: str | None = None,
        extra_remap_rules: Sequence[str] = (),
        properties: str = "SIGNATURE",
        plugin: str | None = None,
        offer_client_rpk_without_key: bool = False) -> ATS:
    """Configure a downstream (edge) ATS that connects to the parent over TLS, pinning its raw public key.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique ATS name within the test.
    :param parent: Parent the edge forwards to.
    :param pin_file: Trusted parent raw public keys (sni.yaml server_rpk_ca), or None for no pin.
    :param policy: proxy.config.ssl.client.verify.server.policy toward the parent.
    :param offer_client_rpk: Offer a raw public key derived from server.pem/server.key, the same
        identity the edge serves inbound, as the edge's client credential toward the parent.
    :param offer_client_x509: Offer the classic X.509 client certificate named by client_cert
        instead.
    :param client_cert: X.509 client certificate offered when offer_client_x509 is set.
    :param client_key: Private key for client_cert.
    :param server_ca_file: CA bundle the parent's X.509 chain is verified against.
    :param extra_remap_rules: remap.config rules matched ahead of the catch-all rule to the parent.
    :param properties: proxy.config.ssl.client.verify.server.properties toward the parent.
    :param plugin: plugin.config line for a test plugin to load, or None.
    :param offer_client_rpk_without_key: Set client_rpk_enabled with no client certificate or key
        anywhere, so there is nothing to offer.
    """

    ats = ats_factory.create(name, enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "server.pubkey.pem",
        SSL_DIRECTORY / "server.wrongpubkey.pem",
    )
    if client_cert != "server.pem":
        ats.copy_to_ssl(SSL_DIRECTORY / client_cert, SSL_DIRECTORY / client_key)
    if server_ca_file is not None:
        ats.copy_to_ssl(SSL_DIRECTORY / server_ca_file)
    load_test_plugin(ats, plugin)
    ats.remap_config.add_lines(list(extra_remap_rules) + [f"map / https://127.0.0.1:{parent.https_port}"])
    ats.set_ssl_multicert_yaml({"ssl_multicert": [dict(DEFAULT_MULTICERT_ENTRY)]})
    ats.records.update(
        {
            "proxy.config.ssl.client.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.private_key.path": str(ats.ssl_directory),
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify|ssl.origin_session_cache",
            "proxy.config.ssl.client.verify.server.policy": policy,
            # SIGNATURE by default, pinning the exact key instead of matching a name: a raw public key
            # carries no SAN.
            "proxy.config.ssl.client.verify.server.properties": properties,
        })
    if server_ca_file is not None:
        ats.records.update(
            {
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": server_ca_file,
            })
    if pin_file is not None or offer_client_rpk or offer_client_x509 or offer_client_rpk_without_key:
        sni_lines = ["sni:", "- fqdn: 127.0.0.1"]
        if pin_file is not None:
            sni_lines.append(f"  server_rpk_ca: {ats.ssl_directory / pin_file}")
        if offer_client_rpk:
            sni_lines += [
                "  client_cert: server.pem",
                "  client_key: server.key",
                "  client_rpk_enabled: true",
            ]
        elif offer_client_x509:
            sni_lines += [
                f"  client_cert: {client_cert}",
                f"  client_key: {client_key}",
            ]
        elif offer_client_rpk_without_key:
            sni_lines.append("  client_rpk_enabled: true")
        ats.write_config_file("sni.yaml", "\n".join(sni_lines) + "\n")
    return ats


def limit_hop_tls_version(edge: ATS, tls_version: str) -> None:
    """Hold the edge's outbound connections, and so the hop to the parent, to one TLS version.

    :param edge: Edge tier.
    :param tls_version: "1.3", or "1.2" to disable outbound TLS 1.3.
    """

    if tls_version == "1.2":
        edge.records.update({"proxy.config.ssl.client.TLSv1_3.enabled": 0})


def load_test_plugin(ats: ATS, plugin: str | None) -> None:
    """Load one of the test plugins built under tests/tools/plugins.

    :param ats: Traffic Server instance to load it into.
    :param plugin: plugin.config line, beginning with the plugin's file name, or None for none.
    """

    if plugin is None:
        return
    ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{plugin.split()[0]}")
    ats.plugin_config.add_line(plugin)


def start_hop(origin: OriginServer, parent: ATS, edge: ATS) -> None:
    """Start the origin, then each tier after the one it forwards to.

    :param origin: Origin behind the parent.
    :param parent: Parent tier.
    :param edge: Edge tier.
    """

    origin.start()
    parent.start()
    edge.start()


def request_through(curl: Curl, edge: ATS) -> CommandResult:
    """Send one request to the edge's TLS listener.

    A rejected hop still returns a 5xx from the edge rather than a transport error, so curl
    succeeds either way and the body says whether the origin was reached.

    :param curl: Curl command runner.
    :param edge: Edge tier receiving the request.
    """

    result = curl.run(f"-k https://127.0.0.1:{edge.https_port}/", timeout=30)
    assert result.returncode == 0, result.output
    return result


def stop_tiers(*tiers: ATS) -> None:
    """Stop each tier so its logs are complete before they are inspected.

    Stopping also fails the test if a tier exited on its own while serving the request.

    :param tiers: ATS instances to stop, closest to the client first.
    """

    for tier in tiers:
        tier.stop()


def assert_log_contains(log: Path, expression: str, explanation: str) -> None:
    """Require a regular expression to match somewhere in a log.

    :param log: Log file to search.
    :param expression: Regular expression searched for.
    :param explanation: Why the match is required, reported on failure.
    """

    content = log.read_text(errors="replace")
    assert re.search(expression, content), f"{explanation}: {expression!r} not found in {log}\n{content}"


def assert_log_excludes(log: Path, expression: str, explanation: str) -> None:
    """Require a regular expression to match nowhere in a log.

    :param log: Log file to search.
    :param expression: Regular expression that must not match.
    :param explanation: Why the match is forbidden, reported on failure.
    """

    content = log.read_text(errors="replace")
    match = re.search(expression, content)
    assert match is None, f"{explanation}: {expression!r} matched {match.group(0)!r} in {log}"


def rewrite_watched_file(path: Path, content: str) -> None:
    """Rewrite a file ATS watches so that the next reload notices it.

    :param path: File to rewrite in place.
    :param content: New contents, which may equal the old ones.
    """

    old_mtime = path.stat().st_mtime
    path.write_text(content)
    # Two writes within one modification-time tick would otherwise look unchanged.
    os.utime(path, (old_mtime + 2, old_mtime + 2))


def reload_config(ats: ATS, token: str) -> dict[str, str]:
    """Reload ATS's configuration and wait for the reload to finish.

    :param ats: Running Traffic Server instance.
    :param token: Reload token, unique within the test.
    :return: The status of every task in the reload, keyed by description and file name.
    """

    result = ats.traffic_ctl("config", "reload", "-m", "-t", token, "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode == 0, result.output
    return reload_task_statuses(ats, token)


def inbound_handshakes(ats: ATS) -> int:
    """Count the TLS handshakes an ATS instance has completed as a server.

    :param ats: Running Traffic Server instance.
    """

    result = ats.traffic_ctl("metric", "get", "proxy.process.ssl.total_success_handshake_count_in")
    assert result.returncode == 0, result.output
    return int(result.stdout.split()[-1])


def origin_session_lookup_keys(log: Path, count: int) -> list[str]:
    """Wait for and return the edge's origin session cache lookup keys, oldest first.

    :param log: The edge's traffic.out.
    :param count: Number of lookups expected so far.
    """

    content = wait_for_file_lines(log, "origin session cache lookup key = ", count)
    return re.findall(r"origin session cache lookup key = (\S+)", content)


@pytest.mark.parametrize("tls_version", ("1.3", "1.2"))
def test_rpk_hop_pin_matches(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, tls_version: str) -> None:
    """Both hops speak RPK and the pin matches, so RPK is negotiated and accepted.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    :param tls_version: TLS version of the hop between the edge and the parent.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_ok", parent, "server.pubkey.pem")
    edge.records.update({"proxy.config.ssl.origin_session_cache.enabled": 1})
    limit_hop_tls_version(edge, tls_version)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should succeed end to end"
    assert_log_contains(
        edge.traffic_out, "Origin authenticated with a raw public key .*pin match=yes",
        "the hop should use RPK, not fall back to X.509")
    # The pinned set identifies the connection in the origin session cache, so a session it
    # authenticated cannot be resumed once the configuration names a different set. A resumed
    # handshake never re-presents the key, so the pin check could not catch that afterwards.
    assert_log_contains(
        edge.traffic_out, "origin session cache lookup key = .*:[0-9a-f]{64}",
        "the pinned key set must identify the cached origin session")


def test_rpk_hop_falls_back_to_x509(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An edge configured for RPK falls back to X.509 against a parent without RPK support.

    This is the steady state for the whole duration of a rolling upgrade. It covers negotiation
    only: the edge does not trust server.pem, so the chain check fails and PERMISSIVE is what lets
    the request through. The outbound X.509 fallback tests cover the chain verdict under ENFORCED.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_x509", origin, rpk_enabled=False)
    edge = configure_edge(ats_factory, "edge_fallback", parent, "server.pubkey.pem", policy="PERMISSIVE")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should still succeed"
    # No RPK was negotiated, so the RPK branch must never run for this hop.
    assert_log_excludes(
        edge.traffic_out, "Origin authenticated with a raw public key", "the hop should quietly negotiate X.509 instead")


def test_rpk_hop_pin_mismatch_enforced(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A pin that does not match the parent's key is fatal under ENFORCED.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_badpin", parent, "server.wrongpubkey.pem")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "the request must not be served"
    assert_log_contains(
        edge.traffic_out, "Origin authenticated with a raw public key .*pin match=no", "the offered key should not match the pin")
    # Warning() goes to diags.log; traffic.out only carries Dbg() debug output.
    assert_log_contains(
        edge.diags_log, "Origin raw public key did not match any trusted key. Action=Terminate",
        "an unmatched pin must terminate the connection under ENFORCED")


def test_rpk_hop_pin_mismatch_permissive(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The same pin mismatch under PERMISSIVE is warned about, but the request is still served.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_badpin_permissive", parent, "server.wrongpubkey.pem", policy="PERMISSIVE")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should still be served"
    assert_log_contains(
        edge.diags_log, "Origin raw public key did not match any trusted key. Action=Continue",
        "an unmatched pin must only warn under PERMISSIVE")


@pytest.mark.parametrize("tls_version", ("1.3", "1.2"))
def test_rpk_hop_mtls_pin_matches(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, tls_version: str) -> None:
    """The parent requires and pins the edge's raw public key, and the pin matches.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    :param tls_version: TLS version of the hop between the edge and the parent.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory, "parent_mtls", origin, rpk_enabled=True, client_rpk_ca_file="server.pubkey.pem", client_cert_level=2)
    edge = configure_edge(ats_factory, "edge_mtls", parent, "server.pubkey.pem", offer_client_rpk=True)
    limit_hop_tls_version(edge, tls_version)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should succeed once the edge's raw public key matches the pin"
    # Without this, an edge falling back to its X.509 client certificate would pass too.
    assert_log_contains(
        parent.traffic_out, "Client authenticated with a raw public key .*pin match=yes",
        "the edge must have authenticated with its raw public key")
    assert_log_excludes(parent.diags_log, "client raw public key did not match any trusted key", "a matching pin must not warn")


def test_rpk_hop_mtls_pin_mismatch(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A required client raw public key that does not match the pin is always fatal.

    Unlike verify_server_policy, inbound mTLS has no PERMISSIVE equivalent that only warns.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory,
        "parent_mtls_badpin",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.wrongpubkey.pem",
        client_cert_level=2)
    edge = configure_edge(ats_factory, "edge_mtls_badpin", parent, "server.pubkey.pem", offer_client_rpk=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "the request must not be served"
    assert_log_contains(
        parent.diags_log, "client raw public key did not match any trusted key", "the offered client key should not match the pin")


def test_rpk_hop_mtls_per_entry_scoping(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A specific multicert entry's ssl_client_rpk_ca_name reaches the connection, not the default entry's.

    The default ("dest_ip: *") entry stays plain X.509 mTLS, while the more specific
    ("dest_ip: 127.0.0.1") entry pins the edge's raw public key. Every connection lands on the
    specific entry because an IP match beats the wildcard, so this only succeeds if that entry's own
    configuration reaches the connection rather than the X.509-only verify path of the default
    entry that SSL_new() started the connection from.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = ats_factory.create("parent_scoped", enable_tls=True, enable_cache=False)
    parent.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "server.pubkey.pem")
    parent.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    parent.set_ssl_multicert_yaml(
        {
            "ssl_multicert":
                [
                    dict(DEFAULT_MULTICERT_ENTRY),
                    {
                        "dest_ip": "127.0.0.1",
                        "ssl_cert_name": "server.pem",
                        "ssl_key_name": "server.key",
                        "ssl_rpk_enabled": 1,
                        "ssl_client_rpk_ca_name": "server.pubkey.pem",
                    },
                ]
        })
    parent.records.update(
        {
            "proxy.config.ssl.client.certification_level": 2,
            "proxy.config.ssl.CA.cert.path": str(parent.ssl_directory),
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify|ssl_load",
        })
    edge = configure_edge(ats_factory, "edge_scoped", parent, "server.pubkey.pem", offer_client_rpk=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should succeed on the specific entry, not the default entry"
    # The default entry has no ssl_client_rpk_ca_name, so if settings leaked from it the RPK-aware verify path
    # would never run and the X.509-only path would reject the edge's raw public key outright.
    # BoringSSL takes the custom_verify path; OpenSSL's classic callback logs the RPK branch directly.
    assert_log_contains(
        parent.traffic_out, "Callback: custom verify client cert|Client authenticated with a raw public key",
        "the matched entry, not the default entry, must drive verification")


def test_rpk_hop_mtls_x509_fallback(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A classic X.509 client certificate still verifies on an RPK-enabled entry.

    Installing custom_verify, without which the RPK branch cannot run at all, disables BoringSSL's automatic
    X.509 verification for the context, so ssl_custom_verify_client_callback()'s X.509 fallback has
    to accept the chain itself from BoringSSL's already-parsed copy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    # server.pem is self-signed, so trusting it directly lets the edge's client certificate, the
    # same file, verify.
    parent = configure_parent(
        ats_factory,
        "parent_mtls_x509",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.pubkey.pem",
        client_cert_level=2,
        client_ca_file="server.pem")
    edge = configure_edge(ats_factory, "edge_mtls_x509", parent, "server.pubkey.pem", offer_client_x509=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "a valid X.509 client cert should still verify on an RPK-enabled entry"
    # Confirms the verification this entry needs engaged, rather than another path accepting the
    # chain. BoringSSL must take its custom_verify fallback; OpenSSL has no such split: one classic
    # callback handles both RPK and X.509.
    expected_callback = (
        "Callback: custom verify client cert" if "BoringSSL" in ats_factory.tls_library() else "Callback: verify client cert")
    assert_log_contains(parent.traffic_out, expected_callback, "the client-cert verify callback for this entry must run")
    assert_log_excludes(
        parent.traffic_out, "client certificate chain verification failed", "the X.509 fallback must accept a validly-signed chain")
    assert_log_excludes(
        parent.traffic_out, "Client authenticated with a raw public key",
        "this connection must take the X.509 path, not the RPK one")


def test_rpk_hop_mtls_x509_untrusted(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The X.509 fallback rejects a client chain from a CA the parent does not trust.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory,
        "parent_mtls_untrusted",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.pubkey.pem",
        client_cert_level=2,
        client_ca_file="server.pem")
    edge = configure_edge(
        ats_factory,
        "edge_mtls_untrusted",
        parent,
        "server.pubkey.pem",
        offer_client_x509=True,
        client_cert="signed-foo.pem",
        client_key="signed-foo.key")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a client cert from an untrusted CA must not authenticate"
    # Assert on the reason, not just the outcome: an edge that sent no client certificate at all
    # would also be rejected, by the empty-chain early return, without running the verification
    # under test.
    assert_log_contains(
        parent.traffic_out, "client certificate chain verification failed: unable to get local issuer certificate",
        "the chain must be rejected for having no trusted issuer")


def test_rpk_hop_mtls_x509_wrong_purpose(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The X.509 fallback enforces certificate purpose, not just chain signatures.

    server.ocsp.pem chains cleanly to ca.ocsp.pem but carries extendedKeyUsage = serverAuth only, so
    it must not authenticate as a client. On BoringSSL, without X509_STORE_CTX_set_default(...,
    "ssl_client") the purpose check never runs and this connection is accepted.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory,
        "parent_mtls_purpose",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.pubkey.pem",
        client_cert_level=2,
        client_ca_file="ca.ocsp.pem")
    edge = configure_edge(
        ats_factory,
        "edge_mtls_purpose",
        parent,
        "server.pubkey.pem",
        offer_client_x509=True,
        client_cert="server.ocsp.pem",
        client_key="server.ocsp.key")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a serverAuth-only cert must not authenticate as a client"
    # This chain verifies, so a generic failure would mean something else rejected it and the
    # purpose check went untested. BoringSSL and OpenSSL word X509_V_ERR_INVALID_PURPOSE differently.
    assert_log_contains(
        parent.traffic_out, "client certificate chain verification failed: (unsupported|unsuitable) certificate purpose",
        "the chain must be rejected on purpose alone, not on its signatures")


def test_rpk_hop_outbound_x509_untrusted(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ENFORCED rejects an origin chain that does not verify on the outbound X.509 fallback.

    The parent offers no raw public key, so the edge's custom_verify path takes its X.509 fallback,
    and the edge does not trust the parent's self-signed certificate.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_x509", origin, rpk_enabled=False)
    edge = configure_edge(ats_factory, "edge_fallback_untrusted", parent, "server.pubkey.pem", policy="ENFORCED")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "an origin chain that does not verify must not be served under ENFORCED"
    assert_log_contains(
        edge.diags_log, "Core server certificate verification failed.*Action=Terminate",
        "ENFORCED must terminate on the chain verdict itself")


def test_rpk_hop_outbound_x509_trusted(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A verifiable origin chain is served under ENFORCED on the outbound X.509 fallback.

    This is the rolling upgrade case the feature exists to serve, with the chain verdict enforced.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_x509", origin, rpk_enabled=False)
    edge = configure_edge(
        ats_factory, "edge_fallback_enforced", parent, "server.pubkey.pem", policy="ENFORCED", server_ca_file="server.pem")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "a trusted origin chain must be served under ENFORCED during a rolling upgrade"
    assert_log_excludes(
        edge.traffic_out, "Origin authenticated with a raw public key", "this hop must fall back to X.509, not negotiate RPK")


def test_rpk_hop_sni_client_ca_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The X.509 fallback honors a per-SNI client CA override on an RPK-enabled entry.

    The verify_client action overrides the client CA on an entry that also pins raw public keys, so
    the fallback has to verify against the store that action installed rather than the one on the
    SSL_CTX. The global CA (server.pem) does not sign this client's certificate and the per-SNI
    override (signer.pem) does, so the connection only succeeds if the per-connection store is what
    the fallback used. Driven by curl rather than an edge, because an ATS hop toward an IP literal
    sends no SNI and so could never match the action. The failure paths that can leave the store
    stale need the context rebuild itself to fail and are not reachable from a test.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory,
        "parent_sni_ca",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.pubkey.pem",
        client_cert_level=2,
        client_ca_file="server.pem",
        sni_client_ca_file="signer.pem")
    origin.start()
    parent.start()

    result = curl.run(
        f"-k --resolve 'random.server.com:{parent.https_port}:127.0.0.1'"
        f" --cert {SSL_DIRECTORY / 'signed-foo.pem'} --key {SSL_DIRECTORY / 'signed-foo.key'}"
        f" https://random.server.com:{parent.https_port}/",
        timeout=30)
    assert result.returncode == 0, result.output
    stop_tiers(parent)

    assert "origin response" in result.output, "a client cert issued by the per-SNI CA override must authenticate"
    # The global CA cannot verify this chain, so reaching the origin proves the fallback used the
    # store the verify_client action installed. A missing store fails closed instead.
    assert_log_excludes(
        parent.diags_log, "no per-connection client CA store", "the per-connection store must be readable from the callback")
    assert_log_excludes(parent.traffic_out, "client certificate chain verification failed", "the override must verify this chain")


@pytest.mark.parametrize("tls_version", ("1.3", "1.2"))
def test_rpk_hop_replaced_pin_set_retires_origin_sessions(
        ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, tls_version: str) -> None:
    """Replacing the edge's pinned set stops a cached origin session from being resumed.

    A resumed handshake never re-presents the key for the pin check to run again, so the cache key
    has to identify the set that authenticated the session.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    :param tls_version: TLS version of the hop between the edge and the parent.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_resume", parent, "server.pubkey.pem")
    edge.records.update(
        {
            # A connection per request, so that each one offers the cached session.
            "proxy.config.http.server_session_sharing.match": "none",
            "proxy.config.ssl.origin_session_cache.enabled": 1,
        })
    limit_hop_tls_version(edge, tls_version)
    start_hop(origin, parent, edge)

    for _ in range(2):
        assert "origin response" in request_through(curl, edge).output
    # Without a resumption here there would be nothing for the replacement to retire.
    content = wait_for_file_lines(edge.traffic_out, REUSED_ORIGIN_SESSION, 1)
    assert content.count(NEW_ORIGIN_SESSION) == 1, content

    rewrite_watched_file(edge.ssl_directory / "server.pubkey.pem", WIDENED_PIN_SET)
    statuses = reload_config(edge, "replace-pin-set")
    assert "sni.yaml" in statuses, statuses

    assert "origin response" in request_through(curl, edge).output
    content = wait_for_file_lines(edge.traffic_out, NEW_ORIGIN_SESSION, 2)
    assert content.count(REUSED_ORIGIN_SESSION) == 1, content
    # The reload also rebuilds the edge's client SSL_CTX, so the key usually changes through the
    # context address alone and the full handshake above would follow even without the digest. The
    # digest is still needed because a new context can reuse the old one's address, and this
    # comparison is what fails when it goes missing.
    keys = origin_session_lookup_keys(edge.traffic_out, 3)
    digests = [key.rsplit(":", 1)[-1] for key in keys]
    assert digests[0] == digests[1] != digests[2], keys


def test_rpk_hop_replaced_pin_set_retires_pooled_connections(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A pooled origin connection is reused only while the edge still pins the key that authenticated it.

    The edge reaches the parent by an IP literal and so sends no SNI, which a name comparison would
    find equal on both sides whatever the pinned set.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_pool", parent, "server.pubkey.pem")
    # The pool is per thread, so one thread keeps every request in reach of the pooled connection.
    edge.records.update({"proxy.config.exec_thread.autoconfig.enabled": 0, "proxy.config.exec_thread.limit": 1})
    start_hop(origin, parent, edge)
    pin_file = edge.ssl_directory / "server.pubkey.pem"

    for _ in range(2):
        assert "origin response" in request_through(curl, edge).output
    # Without reuse here there would be no pooled connection for the replacement to retire.
    assert inbound_handshakes(parent) == 1, "the second request should reuse the pooled connection"

    rewrite_watched_file(pin_file, pin_file.read_text())
    statuses = reload_config(edge, "unchanged-pin-set")
    assert "sni.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    assert inbound_handshakes(parent) == 1, "an unchanged pinned set should keep its pooled connection"

    # Adding a key for rotation leaves the one that authenticated the connection trusted.
    rewrite_watched_file(pin_file, WIDENED_PIN_SET)
    statuses = reload_config(edge, "rotate-pin-set")
    assert "sni.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    assert inbound_handshakes(parent) == 1, "a set that still trusts the key should keep its pooled connection"

    rewrite_watched_file(pin_file, (SSL_DIRECTORY / "server.wrongpubkey.pem").read_text())
    statuses = reload_config(edge, "replace-pin-set")
    assert "sni.yaml" in statuses, statuses
    assert "origin response" not in request_through(curl, edge).output, \
        "a request whose set no longer trusts the key must not reuse the connection that key authenticated"
    stop_tiers(edge, parent)
    assert_log_contains(
        edge.diags_log, "Origin raw public key did not match any trusted key. Action=Terminate",
        "the request must be refused by a new handshake against the replaced set")


@pytest.mark.parametrize("origin_alpn", ("http/1.1", "h2,http/1.1"), ids=("http1", "h2"))
def test_rpk_hop_permissive_connection_not_reused_when_enforced(
        ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, origin_alpn: str) -> None:
    """A connection accepted under PERMISSIVE with an unpinned key does not carry an ENFORCED request.

    The pinned set is the same for every request, so only the key that connection presented and the
    requesting transaction's policy tell them apart. Over HTTP/2 the first request takes the session
    through the connection handoff and the pooled session is multiplexed; each goes through a separate check.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    :param origin_alpn: Protocols the edge offers toward the parent.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(
        ats_factory,
        "edge_policy",
        parent,
        "server.wrongpubkey.pem",
        policy="PERMISSIVE",
        extra_remap_rules=[
            f"map /enforced/ https://127.0.0.1:{parent.https_port}/ @plugin=conf_remap.so"
            " @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED"
        ])
    edge.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.ssl.client.alpn_protocols": origin_alpn,
        })
    start_hop(origin, parent, edge)

    # A PERMISSIVE request would accept the key in a new handshake, so it may use the connection.
    for _ in range(2):
        assert "origin response" in request_through(curl, edge).output, "PERMISSIVE should accept the unpinned key"
    content = wait_for_file_lines(edge.traffic_out, PIN_MISMATCH, 1)
    assert len(re.findall(PIN_MISMATCH, content)) == 1, "the second PERMISSIVE request should reuse the connection"
    result = curl.run(f"-k https://127.0.0.1:{edge.https_port}/enforced/", timeout=30)
    assert result.returncode == 0, result.output
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "an ENFORCED request must not reuse a connection its pin set never matched"
    # Counted rather than read from the warnings, which are throttled.
    assert len(re.findall(PIN_MISMATCH, edge.traffic_out.read_text(errors="replace"))) > 1, \
        "the ENFORCED request must be refused by a handshake of its own"


@pytest.mark.parametrize("tls_version", ("1.3", "1.2"))
def test_rpk_hop_mtls_replaced_pin_set_retires_sessions(
        ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, tls_version: str) -> None:
    """Replacing a parent's trusted client keys stops a session they authenticated from being resumed.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    :param tls_version: TLS version of the hop between the edge and the parent.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory, "parent_mtls", origin, rpk_enabled=True, client_rpk_ca_file="server.pubkey.pem", client_cert_level=2)
    edge = configure_edge(ats_factory, "edge_mtls", parent, "server.pubkey.pem", offer_client_rpk=True)
    edge.records.update(
        {
            # A connection per request, so that each one offers the cached session.
            "proxy.config.http.server_session_sharing.match": "none",
            "proxy.config.ssl.origin_session_cache.enabled": 1,
        })
    limit_hop_tls_version(edge, tls_version)
    start_hop(origin, parent, edge)
    pin_file = parent.ssl_directory / "server.pubkey.pem"

    for _ in range(2):
        assert "origin response" in request_through(curl, edge).output
    # Without a resumption here there would be nothing for the replacement to retire.
    content = wait_for_file_lines(edge.traffic_out, REUSED_ORIGIN_SESSION, 1)
    assert content.count(NEW_ORIGIN_SESSION) == 1, content

    # The identity is derived from the keys, so a reload that leaves them unchanged keeps resuming.
    rewrite_watched_file(pin_file, pin_file.read_text())
    statuses = reload_config(parent, "unchanged-pin-set")
    assert "ssl_multicert.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    content = wait_for_file_lines(edge.traffic_out, REUSED_ORIGIN_SESSION, 2)
    assert content.count(NEW_ORIGIN_SESSION) == 1, content

    rewrite_watched_file(pin_file, WIDENED_PIN_SET)
    statuses = reload_config(parent, "replace-pin-set")
    assert "ssl_multicert.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    content = wait_for_file_lines(edge.traffic_out, NEW_ORIGIN_SESSION, 2)
    assert content.count(REUSED_ORIGIN_SESSION) == 2, content


def test_rpk_hop_sni_client_ca_on_selected_entry(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A verify_client CA override still applies on an entry selected by SNI in a raw public key deployment.

    With raw public keys configured, OpenSSL switches to the selected entry's context while reading
    the ClientHello, which replaces the certificate state a verify_client action had already set up.
    The action's CA must still be the one client certificates are verified against afterwards.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = ats_factory.create("parent_selected", enable_tls=True, enable_cache=False)
    parent.copy_to_ssl(
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "server-foo.pem",
        SSL_DIRECTORY / "server-foo.key",
        SSL_DIRECTORY / "signer.pem",
        SSL_DIRECTORY / "aaa-ca.pem",
    )
    parent.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    # The default entry's raw public key setting is what makes this a raw public key deployment; the
    # entry under test, selected by its name foo.com, has none of its own.
    parent.set_ssl_multicert_yaml(
        {
            "ssl_multicert":
                [
                    dict(DEFAULT_MULTICERT_ENTRY, ssl_rpk_enabled=1),
                    {
                        "ssl_cert_name": "server-foo.pem",
                        "ssl_key_name": "server-foo.key"
                    },
                ]
        })
    parent.write_config_file(
        "sni.yaml",
        "sni:\n"
        "- fqdn: foo.com\n"
        "  verify_client: STRICT\n"
        f"  verify_client_ca_certs: {parent.ssl_directory / 'aaa-ca.pem'}\n",
    )
    parent.records.update(
        {
            "proxy.config.ssl.CA.cert.path": str(parent.ssl_directory),
            "proxy.config.ssl.CA.cert.filename": "signer.pem",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify|ssl_load",
        })
    origin.start()
    parent.start()

    def request(client_cert: str) -> CommandResult:
        """Send one request for foo.com presenting a client certificate.

        :param client_cert: Base name of the client certificate and key under ssl/.
        """

        return curl.run(
            f"-k --resolve 'foo.com:{parent.https_port}:127.0.0.1'"
            f" --cert {SSL_DIRECTORY / (client_cert + '.pem')} --key {SSL_DIRECTORY / (client_cert + '.key')}"
            f" https://foo.com:{parent.https_port}/",
            timeout=30)

    # The positive control: rejecting both certificates would also satisfy the check below.
    assert "origin response" in request("aaa-signed").output, "a certificate from the verify_client CA must authenticate"
    assert "origin response" not in request("signed-bar").output, "a certificate only the global CA trusts must not authenticate"
    stop_tiers(parent)


def test_rpk_hop_outbound_x509_wrong_purpose(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The outbound X.509 fallback enforces certificate purpose, not just chain signatures.

    responder.ocsp.pem chains to ca.ocsp.pem, which the edge trusts, but carries extendedKeyUsage =
    OCSPSigning only, so it must not authenticate the next hop. On BoringSSL, without
    X509_STORE_CTX_set_default(..., "ssl_server") the purpose check never runs and it is accepted.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory, "parent_purpose", origin, rpk_enabled=False, server_cert="responder.ocsp.pem", server_key="responder.ocsp.key")
    edge = configure_edge(ats_factory, "edge_purpose", parent, "server.pubkey.pem", server_ca_file="ca.ocsp.pem")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a certificate not issued for server authentication must not be served"
    # BoringSSL and OpenSSL word X509_V_ERR_INVALID_PURPOSE differently.
    assert_log_contains(
        edge.diags_log,
        "Core server certificate verification failed.*Action=Terminate Error=(unsupported|unsuitable) certificate purpose",
        "the chain must be rejected on purpose alone, not on its signatures")


def test_rpk_hop_outbound_x509_name_mismatch(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The outbound X.509 fallback checks the name when verify_server_properties asks for it.

    The edge reaches the parent by an IP literal, which is not in server.pem, so with ALL a trusted
    chain still fails on its name.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_name", origin, rpk_enabled=False)
    edge = configure_edge(ats_factory, "edge_name", parent, "server.pubkey.pem", server_ca_file="server.pem", properties="ALL")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a certificate that does not name the next hop must not be served"
    assert_log_contains(
        edge.diags_log, r"SNI \(127\.0\.0\.1\) not in certificate\. Action=Terminate", "the chain must be rejected on its name")


def test_rpk_hop_verify_server_hook_rejects_pinned_key(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """TS_EVENT_SSL_VERIFY_SERVER runs for a raw public key and can reject one that matched its pin.

    ssl_verify_test fails every connection that sends no SNI, as one toward an IP literal does.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_hook", parent, "server.pubkey.pem", plugin="ssl_verify_test.so -count=1")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a plugin rejection must stop the request"
    assert_log_contains(
        edge.traffic_out, "Origin authenticated with a raw public key .*pin match=yes",
        "the pin must have matched, so that only the plugin rejected it")
    assert_log_contains(
        edge.diags_log, "TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin raw public key check",
        "the verify hook must run for a raw public key")


def test_rpk_hop_verify_client_hook_rejects_pinned_key(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """TS_EVENT_SSL_VERIFY_CLIENT runs for a raw public key and can reject one that matched its pin.

    ssl_client_verify_test fails a connection with no certificate to check, which is what a raw
    public key presents to it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory,
        "parent_hook",
        origin,
        rpk_enabled=True,
        client_rpk_ca_file="server.pubkey.pem",
        client_cert_level=2,
        plugin="ssl_client_verify_test.so -count=1 -good=random.server.com")
    edge = configure_edge(ats_factory, "edge_mtls", parent, "server.pubkey.pem", offer_client_rpk=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" not in result.output, "a plugin rejection must stop the request"
    assert_log_contains(
        parent.traffic_out, "Client authenticated with a raw public key .*pin match=yes",
        "the pin must have matched, so that only the plugin rejected it")
    assert_log_contains(
        parent.diags_log, "TS_EVENT_SSL_VERIFY_CLIENT plugin failed the client raw public key check",
        "the verify hook must run for a raw public key")


def test_rpk_hop_mtls_reverse_scoping(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A specific X.509-only entry does not take raw public keys from an RPK default entry.

    The default entry pins client raw public keys and the more specific entry the connection lands
    on does not, so the edge's offered raw public key must not be negotiated there; it falls back to
    its X.509 client certificate, which the specific entry verifies.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = ats_factory.create("parent_reverse", enable_tls=True, enable_cache=False)
    parent.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "server.pubkey.pem")
    parent.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    parent.set_ssl_multicert_yaml(
        {
            "ssl_multicert":
                [
                    dict(DEFAULT_MULTICERT_ENTRY, ssl_rpk_enabled=1, ssl_client_rpk_ca_name="server.pubkey.pem"),
                    {
                        "dest_ip": "127.0.0.1",
                        "ssl_cert_name": "server.pem",
                        "ssl_key_name": "server.key"
                    },
                ]
        })
    parent.records.update(
        {
            "proxy.config.ssl.client.certification_level": 2,
            "proxy.config.ssl.CA.cert.path": str(parent.ssl_directory),
            "proxy.config.ssl.CA.cert.filename": "server.pem",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify|ssl_load",
        })
    edge = configure_edge(ats_factory, "edge_reverse", parent, None, offer_client_rpk=True, server_ca_file="server.pem")
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the edge's X.509 client certificate should authenticate on the specific entry"
    assert_log_excludes(
        parent.traffic_out, "Client authenticated with a raw public key", "the specific entry must not negotiate a raw public key")


def test_rpk_hop_offer_without_client_key(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """client_rpk_enabled without a client certificate or key does not fail the connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_nokey", parent, "server.pubkey.pem", offer_client_rpk_without_key=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the connection should proceed without offering a raw public key"
    assert_log_contains(
        edge.diags_log, "client_rpk_enabled has no effect without a client certificate/key",
        "the operator should be told the setting did nothing")


def test_rpk_hop_resumed_connection_is_reused(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A pooled connection that resumed a cached session is reused like one from a full handshake.

    A resumed handshake runs no verify callback, so the key that reuse is checked against has to
    come from the session instead.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_resumed_pool", parent, "server.pubkey.pem")
    edge.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.ssl.origin_session_cache.enabled": 1,
            # Short enough that the first connection leaves the pool, and the next one resumes its
            # session, within the test.
            "proxy.config.http.keep_alive_no_activity_timeout_out": 2,
        })
    start_hop(origin, parent, edge)

    assert "origin response" in request_through(curl, edge).output
    wait_for_metric(edge, "proxy.process.http.current_server_connections", 0)
    assert "origin response" in request_through(curl, edge).output
    wait_for_file_lines(edge.traffic_out, REUSED_ORIGIN_SESSION, 1)
    assert "origin response" in request_through(curl, edge).output
    assert inbound_handshakes(parent) == 2, "the connection that resumed a session should be reused from the pool"
