#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

from collections.abc import Sequence
import json
import re
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SSL_MULTICERT_PARTIAL_RELOAD__valid_sni = "valid.example.com"


def configure_origin(name: str, *, _services: ServiceFactory) -> OriginServer:
    """Create an origin for post-handshake HTTP requests.

    :param name: Unique origin service name.
    :return: Configured origin service.

    :param _services: Test-local services configured by the test.
    """

    origin = _services.origin(name)
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {SSL_MULTICERT_PARTIAL_RELOAD__valid_sni}\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(
        name: str,
        entries: Sequence[str],
        *,
        partial_reload: bool,
        exit_on_load_fail: bool = False,
        _ats_factory: ATSFactory,
        _services: ServiceFactory) -> tuple[ATS, OriginServer]:
    """Configure one TLS proxy and its origin.

    :param name: Unique ATS process-name prefix.
    :param entries: Initial ``ssl_multicert.yaml`` entry lines.
    :param partial_reload: Whether to commit healthy entries from a mixed reload.
    :param exit_on_load_fail: Whether startup must abort on any bad entry.
    :return: Configured ATS and origin processes.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _services: Test-local services configured by the test.
    """

    origin = configure_origin(f"{name}-origin", _services=_services)
    ats = _ats_factory.create(name, enable_tls=True, disable_log_checks=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.multicert.exit_on_load_fail": int(exit_on_load_fail),
            "proxy.config.ssl.server.multicert.partial_reload": int(partial_reload),
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    ats.ssl_multicert_config.add_lines(("ssl_multicert:", *entries))
    return ats, origin


def default_entry() -> tuple[str, ...]:
    """Return a wildcard entry for Uranium's default certificate."""

    return ('  - dest_ip: "*"', "    ssl_cert_name: server.pem", "    ssl_key_name: server.key")


def named_entry(certificate: str, key: str) -> tuple[str, ...]:
    """Return an SNI-only certificate entry.

    :param certificate: Certificate filename below the ATS SSL directory.
    :param key: Private-key filename below the ATS SSL directory.
    :return: YAML lines for one certificate entry.
    """

    return (f"  - ssl_cert_name: {certificate}", f"    ssl_key_name: {key}")


def bad_entry(stem: str = "does_not_exist") -> tuple[str, ...]:
    """Return an entry whose certificate and key are absent.

    :param stem: Missing filename stem.
    :return: YAML lines for one invalid certificate entry.
    """

    return named_entry(f"{stem}.pem", f"{stem}.key")


def write_entries(ats: ATS, entries: Sequence[str]) -> None:
    """Replace the live multicert file.

    :param ats: Running ATS process whose configuration is replaced.
    :param entries: Complete certificate-entry lines.
    """

    ats.ssl_multicert_config.path.write_text("\n".join(("ssl_multicert:", *entries)) + "\n")


def generate_rsa(ats: ATS, stem: str, common_name: str) -> None:
    """Generate a self-signed RSA certificate in ATS's SSL directory.

    :param ats: Running ATS process that owns the SSL directory.
    :param stem: Output certificate and key filename stem.
    :param common_name: Certificate common name.
    """

    result = ats.run(
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-keyout",
        ats.ssl_directory / f"{stem}.key",
        "-out",
        ats.ssl_directory / f"{stem}.pem",
        "-days",
        "365",
        "-nodes",
        "-subj",
        f"/CN={common_name}",
    )
    assert result.returncode == 0, result.output


def generate_ec(ats: ATS, stem: str, common_name: str) -> None:
    """Generate a self-signed prime256v1 certificate.

    :param ats: Running ATS process that owns the SSL directory.
    :param stem: Output certificate and key filename stem.
    :param common_name: Certificate common name.
    """

    key = ats.ssl_directory / f"{stem}.key"
    certificate = ats.ssl_directory / f"{stem}.pem"
    key_result = ats.run("openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key)
    assert key_result.returncode == 0, key_result.output
    cert_result = ats.run(
        "openssl",
        "req",
        "-new",
        "-x509",
        "-key",
        key,
        "-out",
        certificate,
        "-days",
        "365",
        "-nodes",
        "-subj",
        f"/CN={common_name}",
    )
    assert cert_result.returncode == 0, cert_result.output


def reload(ats: ATS, token: str, expected: str) -> None:
    """Reload configuration and wait for the requested terminal state.

    :param ats: Running ATS process to reload.
    :param token: Unique reload token.
    :param expected: Expected terminal state, ``success`` or ``failed``.
    """

    result = ats.traffic_ctl("config", "reload", "--token", token)
    assert result.returncode == 0, result.output
    deadline = time.monotonic() + 30
    latest = {}
    expected_state = "fail" if expected == "failed" else expected
    while time.monotonic() < deadline:
        status = ats.rpc(
            {
                "jsonrpc": "2.0",
                "id": "reload-status",
                "method": "get_reload_config_status",
                "params": {
                    "token": token
                }
            })
        assert status.returncode == 0, status.output
        latest = json.loads(status.stdout)
        assert "error" not in latest, latest
        tasks = latest.get("result", {}).get("tasks", [])
        # A completed child (or the CLI's "0 success" summary) does not mean
        # the root reload has finished installing the new TLS contexts.
        if tasks and all(task["status"] in ("success", "fail", "timeout") for task in tasks):
            assert all(task["status"] == expected_state for task in tasks), latest
            return
        time.sleep(0.1)
    raise AssertionError(f"Reload {token!r} did not become {expected}:\n{latest}")


def request(ats: ATS, hostname: str, expected_common_name: str | None = None, *, _curl: Curl) -> str:
    """Perform a TLS request and optionally check the served certificate.

    :param ats: Running ATS process to query.
    :param hostname: SNI and request hostname.
    :param expected_common_name: Common name expected in curl diagnostics.
    :return: Curl diagnostic output.

    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        ats,
        f"--silent --verbose --insecure --resolve '{hostname}:{ats.https_port}:127.0.0.1' "
        f"'https://{hostname}:{ats.https_port}/'",
    )
    assert result.returncode == 0, result.output
    if expected_common_name:
        assert f"CN={expected_common_name}" in result.stderr
    return result.stderr


def assert_failure_metric(ats: ATS) -> None:
    """Require the multicert load-failure metric to be nonzero.

    :param ats: Running ATS process whose metric is queried.
    """

    result = ats.traffic_ctl("metric", "get", "proxy.process.ssl.ssl_multicert_load_failures")
    assert result.returncode == 0, result.output
    assert re.search(r"proxy\.process\.ssl\.ssl_multicert_load_failures\s+[1-9][0-9]*", result.stdout)


def start(ats: ATS, origin: OriginServer) -> None:
    """Start an origin followed by its ATS proxy.

    :param ats: Configured ATS process.
    :param origin: Configured origin process.
    """

    origin.start()
    ats.start()


def test_ssl_multicert_strict_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Strict reload rejects the entire certificate update.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    ats, origin = configure_ats("strict", default_entry(), partial_reload=False, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    request(ats, SSL_MULTICERT_PARTIAL_RELOAD__valid_sni, _curl=curl)
    write_entries(ats, (*bad_entry(), *default_entry()))
    reload(ats, "strict-mixed", "failed")
    request(ats, SSL_MULTICERT_PARTIAL_RELOAD__valid_sni, "example.com", _curl=curl)
    assert_failure_metric(ats)


def test_ssl_multicert_partial_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Partial reload commits healthy wildcard certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    ats, origin = configure_ats("partial", default_entry(), partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    generate_rsa(ats, "newdefault", "reloaded.example.com")
    replacement = ('  - dest_ip: "*"', "    ssl_cert_name: newdefault.pem", "    ssl_key_name: newdefault.key")
    write_entries(ats, (*bad_entry(), *replacement))
    reload(ats, "partial-mixed", "success")
    request(ats, SSL_MULTICERT_PARTIAL_RELOAD__valid_sni, "reloaded.example.com", _curl=curl)
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert re.search(r"failed to load certificate secret for.*does_not_exist\.pem", diagnostics)
    assert_failure_metric(ats)


def test_ssl_multicert_sni_only_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Partial reload commits healthy SNI-only certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    initial = named_entry("server.pem", "server.key")
    ats, origin = configure_ats("sni-only", initial, partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    request(ats, SSL_MULTICERT_PARTIAL_RELOAD__valid_sni, _curl=curl)
    generate_rsa(ats, "sni-new", "sni-c-new.example.com")
    write_entries(ats, (*bad_entry(), *named_entry("sni-new.pem", "sni-new.key")))
    reload(ats, "sni-only-mixed", "success")
    request(ats, "sni-c-new.example.com", "sni-c-new.example.com", _curl=curl)
    assert_failure_metric(ats)


def test_ssl_multicert_failed_default_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A healthy SNI certificate permits a partial commit when the default fails.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    initial = (*default_entry(), *named_entry("server.pem", "server.key"))
    ats, origin = configure_ats("failed-default", initial, partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    generate_rsa(ats, "sni-d", "sni-d.example.com")
    bad_default = ('  - dest_ip: "*"', "    ssl_cert_name: does_not_exist.pem", "    ssl_key_name: does_not_exist.key")
    write_entries(ats, (*bad_default, *named_entry("sni-d.pem", "sni-d.key")))
    reload(ats, "failed-default-mixed", "success")
    request(ats, "sni-d.example.com", "sni-d.example.com", _curl=curl)
    assert_failure_metric(ats)


def test_ssl_multicert_all_failed_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A partial reload with no healthy certificate remains a failure.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    ats, origin = configure_ats("all-failed", default_entry(), partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    write_entries(ats, (*bad_entry("missing-one"), *bad_entry("missing-two")))
    reload(ats, "all-failed", "failed")
    request(ats, SSL_MULTICERT_PARTIAL_RELOAD__valid_sni, "example.com", _curl=curl)
    assert_failure_metric(ats)


def test_ssl_multicert_ec_and_rsa_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A mixed partial reload commits both EC and RSA certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    ats, origin = configure_ats("ec-rsa", default_entry(), partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    generate_ec(ats, "ecgood", "ec.example.com")
    generate_rsa(ats, "rsagood", "rsa-f.example.com")
    entries = (
        *bad_entry(),
        *named_entry("ecgood.pem", "ecgood.key"),
        *named_entry("rsagood.pem", "rsagood.key"),
    )
    write_entries(ats, entries)
    reload(ats, "ec-rsa-mixed", "success")
    request(ats, "ec.example.com", "ec.example.com", _curl=curl)
    request(ats, "rsa-f.example.com", "rsa-f.example.com", _curl=curl)
    assert_failure_metric(ats)


def test_ssl_multicert_drops_stale_sni(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A failed SNI entry does not retain its certificate after partial commit.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    ats, origin = configure_ats("stale-sni", default_entry(), partial_reload=True, _ats_factory=ats_factory, _services=services)
    start(ats, origin)
    generate_rsa(ats, "alpha", "alpha.example.com")
    generate_rsa(ats, "beta", "beta.example.com")
    initial = (
        *default_entry(),
        *named_entry("alpha.pem", "alpha.key"),
        *named_entry("beta.pem", "beta.key"),
    )
    write_entries(ats, initial)
    reload(ats, "install-alpha-beta", "success")
    request(ats, "alpha.example.com", "alpha.example.com", _curl=curl)
    request(ats, "beta.example.com", "beta.example.com", _curl=curl)

    generate_rsa(ats, "gamma", "gamma.example.com")
    replacement = (
        *default_entry(),
        *bad_entry(),
        *named_entry("beta.pem", "beta.key"),
        *named_entry("gamma.pem", "gamma.key"),
    )
    write_entries(ats, replacement)
    reload(ats, "replace-alpha-with-gamma", "success")
    alpha = request(ats, "alpha.example.com", "example.com", _curl=curl)
    assert "CN=alpha.example.com" not in alpha
    request(ats, "beta.example.com", "beta.example.com", _curl=curl)
    request(ats, "gamma.example.com", "gamma.example.com", _curl=curl)
    assert_failure_metric(ats)


@pytest.mark.parametrize("exit_on_load_fail", [False, True], ids=["continue", "exit"])
def test_ssl_multicert_partial_reload_startup(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    exit_on_load_fail: bool,
) -> None:
    """Initial load still honors ``exit_on_load_fail`` with partial reload enabled.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    :param exit_on_load_fail: Whether the bad certificate must abort startup.
    """

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if curl.uses_uds:
        pytest.skip("ssl_multicert reload coverage requires TCP listeners")

    name = "startup-exit" if exit_on_load_fail else "startup-continue"
    entries = (*default_entry(), *bad_entry())
    ats, _origin = configure_ats(
        name, entries, partial_reload=True, exit_on_load_fail=exit_on_load_fail, _ats_factory=ats_factory, _services=services)
    if exit_on_load_fail:
        ats.expect_start_failure("EMERGENCY: ", return_code=33)
    ats.start()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert "ERROR:" in diagnostics
    if exit_on_load_fail:
        assert "Traffic Server is fully initialized" not in ats.traffic_out.read_text(errors="replace")
