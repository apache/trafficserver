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
import re
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class SslMulticertPartialReloadScenario:
    """Exercise strict, partial, SNI-only, and startup certificate loads."""

    _valid_sni = "valid.example.com"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Retain the process factories used by one scenario.

        :param ats_factory: Factory for isolated ATS processes.
        :param services: Factory for origin services.
        :param curl: Curl command helper.
        """

        if shutil.which("openssl") is None:
            pytest.skip("openssl is required")
        if curl.uses_uds:
            pytest.skip("ssl_multicert reload coverage requires TCP listeners")
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    def configure_origin(self, name: str) -> OriginServer:
        """Create an origin for post-handshake HTTP requests.

        :param name: Unique origin service name.
        :return: Configured origin service.
        """

        origin = self._services.origin(name)
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: {self._valid_sni}\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(
        self,
        name: str,
        entries: Sequence[str],
        *,
        partial_reload: bool,
        exit_on_load_fail: bool = False,
    ) -> tuple[ATS, OriginServer]:
        """Configure one TLS proxy and its origin.

        :param name: Unique ATS process-name prefix.
        :param entries: Initial ``ssl_multicert.yaml`` entry lines.
        :param partial_reload: Whether to commit healthy entries from a mixed reload.
        :param exit_on_load_fail: Whether startup must abort on any bad entry.
        :return: Configured ATS and origin processes.
        """

        origin = self.configure_origin(f"{name}-origin")
        ats = self._ats_factory.create(name, enable_tls=True, disable_log_checks=True)
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

    @staticmethod
    def default_entry() -> tuple[str, ...]:
        """Return a wildcard entry for Uranium's default certificate."""

        return ('  - dest_ip: "*"', "    ssl_cert_name: server.pem", "    ssl_key_name: server.key")

    @staticmethod
    def named_entry(certificate: str, key: str) -> tuple[str, ...]:
        """Return an SNI-only certificate entry.

        :param certificate: Certificate filename below the ATS SSL directory.
        :param key: Private-key filename below the ATS SSL directory.
        :return: YAML lines for one certificate entry.
        """

        return (f"  - ssl_cert_name: {certificate}", f"    ssl_key_name: {key}")

    @staticmethod
    def bad_entry(stem: str = "does_not_exist") -> tuple[str, ...]:
        """Return an entry whose certificate and key are absent.

        :param stem: Missing filename stem.
        :return: YAML lines for one invalid certificate entry.
        """

        return SslMulticertPartialReloadScenario.named_entry(f"{stem}.pem", f"{stem}.key")

    @staticmethod
    def write_entries(ats: ATS, entries: Sequence[str]) -> None:
        """Replace the live multicert file.

        :param ats: Running ATS process whose configuration is replaced.
        :param entries: Complete certificate-entry lines.
        """

        ats.ssl_multicert_config.path.write_text("\n".join(("ssl_multicert:", *entries)) + "\n")

    @staticmethod
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

    @staticmethod
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

    @staticmethod
    def reload(ats: ATS, token: str, expected: str) -> None:
        """Reload configuration and wait for the requested terminal state.

        :param ats: Running ATS process to reload.
        :param token: Unique reload token.
        :param expected: Expected terminal state, ``success`` or ``failed``.
        """

        result = ats.traffic_ctl("config", "reload", "--token", token)
        assert result.returncode == 0, result.output
        deadline = time.monotonic() + 30
        latest = ""
        while time.monotonic() < deadline:
            status = ats.traffic_ctl("config", "status", "--token", token)
            latest = status.output.lower()
            if expected in latest:
                return
            if (expected == "success" and "failed" in latest) or (expected == "failed" and "success" in latest):
                break
            time.sleep(0.1)
        raise AssertionError(f"Reload {token!r} did not become {expected}:\n{latest}")

    def request(self, ats: ATS, hostname: str, expected_common_name: str | None = None) -> str:
        """Perform a TLS request and optionally check the served certificate.

        :param ats: Running ATS process to query.
        :param hostname: SNI and request hostname.
        :param expected_common_name: Common name expected in curl diagnostics.
        :return: Curl diagnostic output.
        """

        result = self._curl.run_for(
            ats,
            f"--silent --verbose --insecure --resolve '{hostname}:{ats.https_port}:127.0.0.1' "
            f"'https://{hostname}:{ats.https_port}/'",
        )
        assert result.returncode == 0, result.output
        if expected_common_name:
            assert f"CN={expected_common_name}" in result.stderr
        return result.stderr

    @staticmethod
    def assert_failure_metric(ats: ATS) -> None:
        """Require the multicert load-failure metric to be nonzero.

        :param ats: Running ATS process whose metric is queried.
        """

        result = ats.traffic_ctl("metric", "get", "proxy.process.ssl.ssl_multicert_load_failures")
        assert result.returncode == 0, result.output
        assert re.search(r"proxy\.process\.ssl\.ssl_multicert_load_failures\s+[1-9][0-9]*", result.stdout)

    @staticmethod
    def start(ats: ATS, origin: OriginServer) -> None:
        """Start an origin followed by its ATS proxy.

        :param ats: Configured ATS process.
        :param origin: Configured origin process.
        """

        origin.start()
        ats.start()

    def run_strict(self) -> None:
        """Reject a mixed reload and retain the previous certificate table."""

        ats, origin = self.configure_ats("strict", self.default_entry(), partial_reload=False)
        self.start(ats, origin)
        self.request(ats, self._valid_sni)
        self.write_entries(ats, (*self.bad_entry(), *self.default_entry()))
        self.reload(ats, "strict-mixed", "failed")
        self.request(ats, self._valid_sni, "example.com")
        self.assert_failure_metric(ats)

    def run_partial(self) -> None:
        """Commit a new wildcard certificate while skipping a bad entry."""

        ats, origin = self.configure_ats("partial", self.default_entry(), partial_reload=True)
        self.start(ats, origin)
        self.generate_rsa(ats, "newdefault", "reloaded.example.com")
        replacement = ('  - dest_ip: "*"', "    ssl_cert_name: newdefault.pem", "    ssl_key_name: newdefault.key")
        self.write_entries(ats, (*self.bad_entry(), *replacement))
        self.reload(ats, "partial-mixed", "success")
        self.request(ats, self._valid_sni, "reloaded.example.com")
        diagnostics = ats.diags_log.read_text(errors="replace")
        assert re.search(r"failed to load certificate secret for.*does_not_exist\.pem", diagnostics)
        self.assert_failure_metric(ats)

    def run_sni_only(self) -> None:
        """Commit a healthy SNI-only certificate without a wildcard entry."""

        initial = self.named_entry("server.pem", "server.key")
        ats, origin = self.configure_ats("sni-only", initial, partial_reload=True)
        self.start(ats, origin)
        self.request(ats, self._valid_sni)
        self.generate_rsa(ats, "sni-new", "sni-c-new.example.com")
        self.write_entries(ats, (*self.bad_entry(), *self.named_entry("sni-new.pem", "sni-new.key")))
        self.reload(ats, "sni-only-mixed", "success")
        self.request(ats, "sni-c-new.example.com", "sni-c-new.example.com")
        self.assert_failure_metric(ats)

    def run_failed_default(self) -> None:
        """Commit a healthy SNI certificate when the wildcard entry fails."""

        initial = (*self.default_entry(), *self.named_entry("server.pem", "server.key"))
        ats, origin = self.configure_ats("failed-default", initial, partial_reload=True)
        self.start(ats, origin)
        self.generate_rsa(ats, "sni-d", "sni-d.example.com")
        bad_default = ('  - dest_ip: "*"', "    ssl_cert_name: does_not_exist.pem", "    ssl_key_name: does_not_exist.key")
        self.write_entries(ats, (*bad_default, *self.named_entry("sni-d.pem", "sni-d.key")))
        self.reload(ats, "failed-default-mixed", "success")
        self.request(ats, "sni-d.example.com", "sni-d.example.com")
        self.assert_failure_metric(ats)

    def run_all_failed(self) -> None:
        """Reject a partial reload when no user certificate can be loaded."""

        ats, origin = self.configure_ats("all-failed", self.default_entry(), partial_reload=True)
        self.start(ats, origin)
        self.write_entries(ats, (*self.bad_entry("missing-one"), *self.bad_entry("missing-two")))
        self.reload(ats, "all-failed", "failed")
        self.request(ats, self._valid_sni, "example.com")
        self.assert_failure_metric(ats)

    def run_ec_and_rsa(self) -> None:
        """Commit both EC and RSA SNI certificates beside a bad entry."""

        ats, origin = self.configure_ats("ec-rsa", self.default_entry(), partial_reload=True)
        self.start(ats, origin)
        self.generate_ec(ats, "ecgood", "ec.example.com")
        self.generate_rsa(ats, "rsagood", "rsa-f.example.com")
        entries = (
            *self.bad_entry(),
            *self.named_entry("ecgood.pem", "ecgood.key"),
            *self.named_entry("rsagood.pem", "rsagood.key"),
        )
        self.write_entries(ats, entries)
        self.reload(ats, "ec-rsa-mixed", "success")
        self.request(ats, "ec.example.com", "ec.example.com")
        self.request(ats, "rsa-f.example.com", "rsa-f.example.com")
        self.assert_failure_metric(ats)

    def run_stale_sni(self) -> None:
        """Drop a previously healthy SNI certificate during a partial commit."""

        ats, origin = self.configure_ats("stale-sni", self.default_entry(), partial_reload=True)
        self.start(ats, origin)
        self.generate_rsa(ats, "alpha", "alpha.example.com")
        self.generate_rsa(ats, "beta", "beta.example.com")
        initial = (
            *self.default_entry(),
            *self.named_entry("alpha.pem", "alpha.key"),
            *self.named_entry("beta.pem", "beta.key"),
        )
        self.write_entries(ats, initial)
        self.reload(ats, "install-alpha-beta", "success")
        self.request(ats, "alpha.example.com", "alpha.example.com")
        self.request(ats, "beta.example.com", "beta.example.com")

        self.generate_rsa(ats, "gamma", "gamma.example.com")
        replacement = (
            *self.default_entry(),
            *self.bad_entry(),
            *self.named_entry("beta.pem", "beta.key"),
            *self.named_entry("gamma.pem", "gamma.key"),
        )
        self.write_entries(ats, replacement)
        self.reload(ats, "replace-alpha-with-gamma", "success")
        alpha = self.request(ats, "alpha.example.com", "example.com")
        assert "CN=alpha.example.com" not in alpha
        self.request(ats, "beta.example.com", "beta.example.com")
        self.request(ats, "gamma.example.com", "gamma.example.com")
        self.assert_failure_metric(ats)

    def run_startup(self, *, exit_on_load_fail: bool) -> None:
        """Verify partial reload never hides initial-load failures.

        :param exit_on_load_fail: Whether the bad entry must abort startup.
        """

        name = "startup-exit" if exit_on_load_fail else "startup-continue"
        entries = (*self.default_entry(), *self.bad_entry())
        ats, _origin = self.configure_ats(
            name,
            entries,
            partial_reload=True,
            exit_on_load_fail=exit_on_load_fail,
        )
        if exit_on_load_fail:
            ats.expect_start_failure("EMERGENCY: ", return_code=33)
        ats.start()
        diagnostics = ats.diags_log.read_text(errors="replace")
        assert "ERROR:" in diagnostics
        if exit_on_load_fail:
            assert "Traffic Server is fully initialized" not in ats.traffic_out.read_text(errors="replace")


def test_ssl_multicert_strict_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Strict reload rejects the entire certificate update.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_strict()


def test_ssl_multicert_partial_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Partial reload commits healthy wildcard certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_partial()


def test_ssl_multicert_sni_only_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Partial reload commits healthy SNI-only certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_sni_only()


def test_ssl_multicert_failed_default_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A healthy SNI certificate permits a partial commit when the default fails.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_failed_default()


def test_ssl_multicert_all_failed_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A partial reload with no healthy certificate remains a failure.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_all_failed()


def test_ssl_multicert_ec_and_rsa_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A mixed partial reload commits both EC and RSA certificates.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_ec_and_rsa()


def test_ssl_multicert_drops_stale_sni(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A failed SNI entry does not retain its certificate after partial commit.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_stale_sni()


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

    SslMulticertPartialReloadScenario(ats_factory, services, curl).run_startup(exit_on_load_fail=exit_on_load_fail)
