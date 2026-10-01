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


def tls_hook_configure_origin(services: ServiceFactory) -> VerifierServer:
    """Configure the common TLS origin response.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("origin", "tls_hooks.replay.yaml", http_ports=[])


def tls_hook_configure_ats(
        ats_factory: ATSFactory, disable_tls_13: bool, *, _origin: VerifierServer, _plugin_arguments: str) -> ATS:
    """Configure TLS, the origin mapping, and the selected hook callbacks.

    :param _origin: Test-local origin configured by the test.
    :param _plugin_arguments: Test-local plugin arguments configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param disable_tls_13: Disable tls 13 used by this test step.
    """

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
    ats.remap_config.add_line(f"map https://example.com:{ats.https_port} https://127.0.0.1:{_origin.https_port}")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
    ats.plugin_config.add_line(f"ssl_hook_test.so {_plugin_arguments}")
    return ats


def tls_hook_request(*, _ats: ATS, _curl: Curl) -> None:
    """Send the common TLS request through ATS.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--insecure --header 'host:example.com:{_ats.https_port}' --header 'uuid: tls-hook' "
            f"'https://127.0.0.1:{_ats.https_port}/'"),
        timeout=15,
    )
    assert result.returncode == 0, result.output


def tls_hook_verify_hook_diagnostics(*, _ats: ATS, _gold_file: Path) -> None:
    """Compare the callback trace with the original wildcard gold file.

    :param _ats: Test-local ats configured by the test.
    :param _gold_file: Test-local gold file configured by the test.
    """

    deadline = time.monotonic() + 5
    error: AssertionError | None = None
    while time.monotonic() < deadline:
        output = _ats.traffic_out.read_text(errors="replace")
        outbound_expectations = {
            "ts-out-start-close-2.gold": (
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
        if expected := outbound_expectations.get(_gold_file.name):
            positions = [output.find(expression) for expression in expected]
            if all(position >= 0 for position in positions) and positions == sorted(positions):
                return
            error = AssertionError(f"Expected ordered TLS hook diagnostics {expected!r}:\n{output}")
            time.sleep(0.1)
            continue
        try:
            assert_matches_gold(output, _gold_file)
            return
        except AssertionError as current_error:
            error = current_error
            time.sleep(0.1)
    assert error is not None
    raise error


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
    """TLS handshake hook combinations run in their documented order.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param plugin_arguments: Plugin arguments used by this test step.
    :param gold_file: Path to the gold file.
    :param disable_tls_13: Disable tls 13 used by this test step.
    """
    _gold_file = Path(__file__).parent / "gold" / gold_file
    _origin = tls_hook_configure_origin(services)
    _ats = tls_hook_configure_ats(ats_factory, disable_tls_13, _origin=_origin, _plugin_arguments=plugin_arguments)

    _origin.start()
    _ats.start()
    tls_hook_request(_ats=_ats, _curl=curl)
    tls_hook_verify_hook_diagnostics(_ats=_ats, _gold_file=_gold_file)


def parked_tls_close_request(*, _ats: ATS, _curl: Curl) -> None:
    """Time out the client while the plugin still has the handshake parked.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        (f"--insecure --max-time 1 --header 'host:example.com:{_ats.https_port}' "
         f"'https://127.0.0.1:{_ats.https_port}/'"),
        timeout=15,
    )
    assert result.returncode == 28, result.output


def parked_tls_close_verify_hook_diagnostics(*, _ats: ATS) -> None:
    """Verify both the parked client-hello and close callbacks ran.

    :param _ats: Test-local ats configured by the test.
    """

    deadline = time.monotonic() + 5
    output = ""
    while time.monotonic() < deadline:
        output = _ats.traffic_out.read_text(errors="replace")
        if "Client Hello callback 0" in output and "Close callback 0" in output and "event is good" in output:
            return
        time.sleep(0.1)
    raise AssertionError(f"The parked connection did not dispatch its close hook:\n{output}")


def parked_tls_close_configure_ats(
        ats_factory: ATSFactory, disable_tls_13: bool, *, _origin: VerifierServer, _plugin_arguments: str) -> ATS:
    """Configure TLS, the origin mapping, and the selected hook callbacks.

    :param _origin: Test-local origin configured by the test.
    :param _plugin_arguments: Test-local plugin arguments configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param disable_tls_13: Disable tls 13 used by this test step.
    """

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
    ats.remap_config.add_line(f"map https://example.com:{ats.https_port} https://127.0.0.1:{_origin.https_port}")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
    ats.plugin_config.add_line(f"ssl_hook_test.so {_plugin_arguments}")
    return ats


def test_tls_hook_close_while_parked(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A timeout while parked still dispatches the close hook.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    disable_tls_13 = False
    plugin_arguments = "-client_hello=1 -close=1"
    gold_file = "ts-client-hello-delayed-1.gold"
    _gold_file = Path(__file__).parent / "gold" / gold_file
    _origin = tls_hook_configure_origin(services)
    _ats = parked_tls_close_configure_ats(ats_factory, disable_tls_13, _origin=_origin, _plugin_arguments=plugin_arguments)
    _ats.records.update({"proxy.config.ssl.handshake_timeout_in": 1})

    _origin.start()
    _ats.start()
    parked_tls_close_request(_ats=_ats, _curl=curl)
    parked_tls_close_verify_hook_diagnostics(_ats=_ats)
