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

from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_file_lines, wait_for_metric


@pytest.mark.parametrize("hook", ("vconn_start", "client_hello", "cert"))
@pytest.mark.parametrize("reject_async", (False, True), ids=("synchronous", "asynchronous"))
def test_tls_hooks_reject(ats_factory: ATSFactory, services: ServiceFactory, hook: str, reject_async: bool) -> None:
    """A rejected TLS hook closes and releases each connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the raw TLS rejection probes.
    :param hook: TLS hook at which the plugin rejects the connection.
    :param reject_async: Whether to reenable from a scheduled continuation.
    """
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "tls_hook_reject|ssl",
    })
    ats.copy_custom_plugin("{AtsTestPluginsDir}/tls_hook_reject.so")
    ats.plugin_config.add_line(f"tls_hook_reject.so --hook={hook}" + (" --async" if reject_async else ""))
    ats.start()
    for index in range(2):
        probe = services.process(f"probe-{index}", (sys.executable, Path(__file__).parent / "reject_probe.py", str(ats.https_port)))
        result = probe.run()
        assert result.returncode == 0, result.output
        assert "ATS rejected the connection" in result.stdout
    expression = (
        r"Rejecting connection .* asynchronously"
        if reject_async else r"Rejecting connection 0x[0-9a-f]+(?![0-9a-f]| asynchronously)")
    wait_for_file_lines(ats.traffic_out, expression, 1)
    wait_for_metric(ats, "proxy.process.net.connections_currently_open", 0)
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
