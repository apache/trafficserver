#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements. See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership. The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License. You may obtain a copy of the License at
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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines, wait_for_metric

# ssl_hook_test.so arguments that make it hold each hook for two seconds before
# reenabling it.
_DELAYING_PLUGIN_ARGUMENTS = {
    "vconn_start": "-d=1",
    "client_hello": "-client_hello=1",
    "cert": "-cert=1",
}


def _start_rejecting_ats(ats_factory: ATSFactory, plugin_lines: list[str], debug_tags: str) -> ATS:
    """Start Traffic Server with tls_hook_reject.so and any later plugins.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param plugin_lines: plugin.config lines, in hook invocation order.
    :param debug_tags: Debug tags to enable in traffic.out.
    """
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": debug_tags,
        }
    )
    ats.copy_custom_plugin("{AtsTestPluginsDir}/tls_hook_reject.so")
    if any(line.startswith("ssl_hook_test.so") for line in plugin_lines):
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
    for line in plugin_lines:
        ats.plugin_config.add_line(line)
    ats.start()
    return ats


def _probe_rejections(ats: ATS, services: ServiceFactory, count: int) -> None:
    """Verify that ATS rejects each of count TLS handshakes.

    :param ats: Traffic Server instance under test.
    :param services: Factory for the raw TLS rejection probes.
    :param count: Number of sequential probes to run.
    """
    for index in range(count):
        probe = services.process(f"probe-{index}", (sys.executable, Path(__file__).parent / "reject_probe.py", str(ats.https_port)))
        result = probe.run()
        assert result.returncode == 0, result.output
        assert "ATS rejected the connection" in result.stdout


def _assert_clean_diagnostics(ats: ATS) -> None:
    """Fail if Traffic Server logged an error while handling the rejections.

    :param ats: Stopped Traffic Server instance under test.
    """
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics


@pytest.mark.parametrize("hook", ("vconn_start", "client_hello", "cert"))
@pytest.mark.parametrize("reject_async", (False, True), ids=("synchronous", "asynchronous"))
def test_tls_hooks_reject(ats_factory: ATSFactory, services: ServiceFactory, hook: str, reject_async: bool) -> None:
    """A rejected TLS hook closes and releases each connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the raw TLS rejection probes.
    :param hook: TLS hook at which the plugin rejects the connection.
    :param reject_async: Whether to reenable from a scheduled continuation.
    """
    ats = _start_rejecting_ats(
        ats_factory, [f"tls_hook_reject.so --hook={hook}" + (" --async" if reject_async else "")], "tls_hook_reject|ssl"
    )
    _probe_rejections(ats, services, 2)
    expression = (
        r"Rejecting connection .* asynchronously"
        if reject_async
        else r"Rejecting connection 0x[0-9a-f]+(?![0-9a-f]| asynchronously)"
    )
    wait_for_file_lines(ats.traffic_out, expression, 1)
    wait_for_metric(ats, "proxy.process.net.connections_currently_open", 0)
    ats.stop()
    # A rejection must end the handshake itself. The certificate callback's
    # renegotiation exit also closes a ClientHello rejection, which would hide a
    # missing rejection check in the ClientHello callback.
    assert "trying to renegotiate from the client" not in ats.traffic_out.read_text(errors="replace")
    _assert_clean_diagnostics(ats)


@pytest.mark.parametrize("hook", ("vconn_start", "client_hello", "cert"))
def test_tls_hooks_reject_before_delaying_plugin(ats_factory: ATSFactory, services: ServiceFactory, hook: str) -> None:
    """A rejection waits for a later plugin on the same hook to reenable.

    The rejecting plugin's reenable passes the connection to the next plugin on
    the hook. Closing it before that plugin reenables would leave the plugin
    holding a freed connection, and its reenable would crash traffic_server.
    ssl_hook_test.so does not watch TS_VCONN_CLOSE_HOOK here, so it has no way
    to learn of an early close.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the raw TLS rejection probes.
    :param hook: TLS hook at which both plugins run.
    """
    ats = _start_rejecting_ats(
        ats_factory,
        [f"tls_hook_reject.so --hook={hook}", f"ssl_hook_test.so {_DELAYING_PLUGIN_ARGUMENTS[hook]}"],
        "tls_hook_reject|ssl_hook_test|ssl",
    )
    _probe_rejections(ats, services, 1)
    # The connection must close only after ssl_hook_test.so reenables it.
    wait_for_file_lines(ats.traffic_out, r"(?s)Callback reenable ssl_vc=.*plugin rejected the TLS handshake", 1)
    wait_for_metric(ats, "proxy.process.net.connections_currently_open", 0)
    ats.stop()
    assert "traffic_crashlog" not in ats.traffic_out.read_text(errors="replace")
    _assert_clean_diagnostics(ats)
