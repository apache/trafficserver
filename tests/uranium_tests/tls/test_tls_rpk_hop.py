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
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines

from ..jsonrpc.config_reload_helpers import reload_task_statuses

SSL_DIRECTORY = Path(__file__).parent / "ssl"

# The single-entry ssl_multicert.yaml both tiers serve their inbound TLS from. server.pubkey.pem is
# this certificate's public key, so it is what the pinning scenarios pin.
DEFAULT_MULTICERT_ENTRY = {"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}

# A different trusted set that still contains server.pem's key, so a connection pinned against it
# succeeds and only the set's identity changes.
WIDENED_PIN_SET = (SSL_DIRECTORY / "server.pubkey.pem").read_text() + (SSL_DIRECTORY / "server.wrongpubkey.pem").read_text()

NEW_ORIGIN_SESSION = "new session to origin server"
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
        sni_client_ca_file: str | None = None) -> ATS:
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
    """

    ats = ats_factory.create(name, enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key")
    for filename in (client_rpk_ca_file, client_ca_file, sni_client_ca_file):
        if filename is not None:
            ats.copy_to_ssl(SSL_DIRECTORY / filename)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")

    entry: dict[str, object] = dict(DEFAULT_MULTICERT_ENTRY)
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
        server_ca_file: str | None = None) -> ATS:
    """Configure a downstream (edge) ATS that connects to the parent over TLS, pinning its raw public key.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique ATS name within the test.
    :param parent: Parent the edge forwards to.
    :param pin_file: Trusted parent raw public keys (sni.yaml server_rpk_ca), or None for no pin.
    :param policy: proxy.config.ssl.client.verify.server.policy toward the parent.
    :param offer_client_rpk: Offer a raw public key derived from server.pem/server.key, the same
        identity the edge serves inbound, as the edge's client certificate toward the parent.
    :param offer_client_x509: Offer the classic X.509 client certificate named by client_cert
        instead.
    :param client_cert: X.509 client certificate offered when offer_client_x509 is set.
    :param client_key: Private key for client_cert.
    :param server_ca_file: CA bundle the parent's X.509 chain is verified against.
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
    ats.remap_config.add_line(f"map / https://127.0.0.1:{parent.https_port}")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [dict(DEFAULT_MULTICERT_ENTRY)]})
    ats.records.update(
        {
            "proxy.config.ssl.client.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.private_key.path": str(ats.ssl_directory),
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify|ssl.origin_session_cache",
            "proxy.config.ssl.client.verify.server.policy": policy,
            # Pin the exact key instead of matching a name: a raw public key carries no SAN.
            "proxy.config.ssl.client.verify.server.properties": "SIGNATURE",
        })
    if server_ca_file is not None:
        ats.records.update(
            {
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": server_ca_file,
            })
    if pin_file is not None or offer_client_rpk or offer_client_x509:
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
        ats.write_config_file("sni.yaml", "\n".join(sni_lines) + "\n")
    return ats


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


def test_rpk_hop_pin_matches(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Both hops speak RPK and the pin matches, so RPK is negotiated and accepted.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(ats_factory, "parent_rpk", origin, rpk_enabled=True)
    edge = configure_edge(ats_factory, "edge_ok", parent, "server.pubkey.pem")
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


def test_rpk_hop_mtls_pin_matches(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The parent requires and pins the edge's raw public key, and the pin matches.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
    """

    origin = configure_origin(services)
    parent = configure_parent(
        ats_factory, "parent_mtls", origin, rpk_enabled=True, client_rpk_ca_file="server.pubkey.pem", client_cert_level=2)
    edge = configure_edge(ats_factory, "edge_mtls", parent, "server.pubkey.pem", offer_client_rpk=True)
    start_hop(origin, parent, edge)

    result = request_through(curl, edge)
    stop_tiers(edge, parent)

    assert "origin response" in result.output, "the request should succeed once the client cert pin matches"
    assert_log_excludes(parent.diags_log, "client raw public key did not match any trusted key", "a matching pin must not warn")


def test_rpk_hop_mtls_pin_mismatch(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A required client certificate whose pin does not match is always fatal.

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
    """A specific multicert entry's client_rpk_ca reaches the connection, not the default entry's.

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
    # The default entry has no client_rpk_ca, so if settings leaked from it the RPK-aware verify path
    # would never run and the X.509-only path would reject the edge's raw public key outright.
    # BoringSSL takes the custom_verify path; OpenSSL's classic callback logs the RPK branch directly.
    assert_log_contains(
        parent.traffic_out, "Callback: custom verify client cert|Client authenticated with a raw public key",
        "the matched entry, not the default entry, must drive verification")


def test_rpk_hop_mtls_x509_fallback(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A classic X.509 client certificate still verifies on an RPK-enabled entry.

    Installing custom_verify, which the RPK branch needs at all, disables BoringSSL's automatic
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
    # Confirms client-certificate verification engaged rather than an unrelated path silently
    # accepting the chain. OpenSSL has no custom_verify split: one classic callback handles both RPK
    # and X.509, so its entry log is the signal there.
    assert_log_contains(
        parent.traffic_out, "Callback: custom verify client cert|Callback: verify client cert",
        "a client-cert verify callback must run for this entry")
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


def test_rpk_hop_replaced_pin_set_retires_origin_sessions(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Replacing the edge's pinned set stops a cached origin session from being resumed.

    A resumed handshake never re-presents the key for the pin check to run again, so the cache key
    has to identify the set that authenticated the session.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
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
    keys = origin_session_lookup_keys(edge.traffic_out, 3)
    digests = [key.rsplit(":", 1)[-1] for key in keys]
    assert digests[0] == digests[1] != digests[2], keys


def test_rpk_hop_replaced_pin_set_retires_pooled_connections(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A pooled origin connection is reused only while the edge still pins the set that authenticated it.

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

    # The identity is derived from the keys, so a reload that leaves them unchanged keeps the pool.
    rewrite_watched_file(pin_file, pin_file.read_text())
    statuses = reload_config(edge, "unchanged-pin-set")
    assert "sni.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    assert inbound_handshakes(parent) == 1, "an unchanged pinned set should keep its pooled connection"

    rewrite_watched_file(pin_file, WIDENED_PIN_SET)
    statuses = reload_config(edge, "replace-pin-set")
    assert "sni.yaml" in statuses, statuses
    assert "origin response" in request_through(curl, edge).output
    assert inbound_handshakes(parent) == 2, "a replaced pinned set must not reuse the connection the old set authenticated"


def test_rpk_hop_mtls_replaced_pin_set_retires_sessions(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Replacing a parent's trusted client keys stops a session they authenticated from being resumed.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Curl command runner.
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
