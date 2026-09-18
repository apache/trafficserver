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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer, assert_matches_gold


class TLSHookScenario:
    """Drive one combination of ssl_hook_test handshake callbacks."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        plugin_arguments: str,
        gold_file: str,
        *,
        disable_tls_13: bool = False,
    ) -> None:
        self._curl = curl
        self._plugin_arguments = plugin_arguments
        self._gold_file = Path(__file__).parent / "gold" / gold_file
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory, disable_tls_13)

    def configure_origin(self, services: ServiceFactory) -> VerifierServer:
        """Configure the common TLS origin response."""

        return services.verifier_server("origin", "tls_hooks.replay.yaml", http_ports=[])

    def configure_ats(self, ats_factory: ATSFactory, disable_tls_13: bool) -> ATS:
        """Configure TLS, the origin mapping, and the selected hook callbacks."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        records: dict[str, object] = {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.show_location": 0,
            "proxy.config.diags.debug.tags": "ssl_hook_test",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        }
        if disable_tls_13:
            records["proxy.config.ssl.TLSv1_3.enabled"] = 0
        ats.records.update(records)
        ats.remap_config.add_line(f"map https://example.com:{ats.https_port} https://127.0.0.1:{self._origin.https_port}")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
        ats.plugin_config.add_line(f"ssl_hook_test.so {self._plugin_arguments}")
        return ats

    def request(self) -> None:
        """Send the common TLS request through ATS."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--insecure --header 'host:example.com:{self._ats.https_port}' --header 'uuid: tls-hook' "
                f"'https://127.0.0.1:{self._ats.https_port}/'"),
            timeout=15,
        )
        assert result.returncode == 0, result.output

    def verify_hook_diagnostics(self) -> None:
        """Compare the callback trace with the original wildcard gold file."""

        deadline = time.monotonic() + 5
        error: AssertionError | None = None
        while time.monotonic() < deadline:
            output = self._ats.traffic_out.read_text(errors="replace")
            outbound_expectations = {
                "ts-out-start-close-2.gold":
                    (
                        "Outbound start callback 0",
                        "Outbound close callback 0",
                        "Outbound close callback 1",
                    ),
                "ts-out-delay-start-2.gold": (
                    "Outbound delay start callback 0",
                    "Outbound delay start callback 1",
                ),
                "ts-close-out-close.gold": (
                    "Outbound close callback",
                    "Close callback 0",
                    "Close callback 1",
                ),
            }
            if expected := outbound_expectations.get(self._gold_file.name):
                positions = [output.find(expression) for expression in expected]
                if all(position >= 0 for position in positions) and positions == sorted(positions):
                    return
                error = AssertionError(f"Expected ordered TLS hook diagnostics {expected!r}:\n{output}")
                time.sleep(0.1)
                continue
            try:
                assert_matches_gold(output, self._gold_file)
                return
            except AssertionError as current_error:
                error = current_error
                time.sleep(0.1)
        assert error is not None
        raise error

    def run(self) -> None:
        """Start the origin and ATS, send a request, and validate callbacks."""

        self._origin.start()
        self._ats.start()
        self.request()
        self.verify_hook_diagnostics()


_HOOK_CASES = (
    pytest.param("-preaccept=1", "ts-preaccept-1.gold", True, id="preaccept-one"),
    pytest.param("-sni=1", "ts-sni-1.gold", False, id="sni-one"),
    pytest.param("-cert=1", "ts-cert-1.gold", False, id="certificate-one"),
    pytest.param("-cert=1 -sni=1 -preaccept=1", "ts-preaccept1-sni1-cert1.gold", False, id="combined-hooks"),
    pytest.param("-preaccept=2", "ts-preaccept-2.gold", False, id="preaccept-two"),
    pytest.param("-sni=2", "ts-sni-2.gold", False, id="sni-two"),
    pytest.param("-cert=2", "ts-cert-2.gold", False, id="certificate-two"),
    pytest.param("-i=1", "ts-cert-im-1.gold", False, id="immediate-certificate"),
    pytest.param("-cert=1 -i=2", "ts-cert-1-im-2.gold", False, id="certificate-and-immediate"),
    pytest.param("-d=1", "ts-preaccept-delayed-1.gold", False, id="delayed-preaccept"),
    pytest.param("-p=2 -d=1", "ts-preaccept-delayed-1-immdate-2.gold", False, id="preaccept-and-delayed"),
    pytest.param("-out_start=1 -out_close=2", "ts-out-start-close-2.gold", False, id="outbound-start-close"),
    pytest.param("-out_start_delay=2", "ts-out-delay-start-2.gold", False, id="outbound-delayed-start"),
    pytest.param("-close=2 -out_close=1", "ts-close-out-close.gold", False, id="inbound-outbound-close"),
    pytest.param("-client_hello_imm=1", "ts-client-hello-1.gold", False, id="immediate-client-hello"),
    pytest.param("-client_hello=1 -close=1", "ts-client-hello-delayed-1.gold", False, id="delayed-client-hello"),
    pytest.param("-client_hello=2 -close=1", "ts-client-hello-2.gold", False, id="two-client-hello-hooks"),
)


@pytest.mark.parametrize(("plugin_arguments", "gold_file", "disable_tls_13"), _HOOK_CASES)
def test_tls_hook_combination(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    plugin_arguments: str,
    gold_file: str,
    disable_tls_13: bool,
) -> None:
    """TLS handshake hook combinations run in their documented order."""

    TLSHookScenario(
        ats_factory,
        services,
        curl,
        plugin_arguments,
        gold_file,
        disable_tls_13=disable_tls_13,
    ).run()


class ParkedTLSCloseScenario(TLSHookScenario):
    """Verify VCONN_CLOSE runs when a parked TLS handshake times out."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        super().__init__(ats_factory, services, curl, "-client_hello=1 -close=1", "ts-client-hello-delayed-1.gold")
        self._ats.records.update({"proxy.config.ssl.handshake_timeout_in": 1})

    def request(self) -> None:
        """Time out the client while the plugin still has the handshake parked."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--insecure --max-time 1 --header 'host:example.com:{self._ats.https_port}' "
                f"'https://127.0.0.1:{self._ats.https_port}/'"),
            timeout=15,
        )
        assert result.returncode == 28, result.output

    def verify_hook_diagnostics(self) -> None:
        """Verify both the parked client-hello and close callbacks ran."""

        deadline = time.monotonic() + 5
        output = ""
        while time.monotonic() < deadline:
            output = self._ats.traffic_out.read_text(errors="replace")
            if "Client Hello callback 0" in output and "Close callback 0" in output and "event is good" in output:
                return
            time.sleep(0.1)
        raise AssertionError(f"The parked connection did not dispatch its close hook:\n{output}")


def test_tls_hook_close_while_parked(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A timeout while parked still dispatches the close hook."""

    ParkedTLSCloseScenario(ats_factory, services, curl).run()
