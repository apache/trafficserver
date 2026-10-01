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
import subprocess

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    Curl,
    HttpBinServer,
    ProceduralContext,
    ServiceFactory,
    wait_for_file_lines,
)


def test_test_TSVConnPPInfo(
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """The TSVConn API reports Proxy Protocol version, transport, and addresses.

    :param procedural_context: Procedural context used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    help_text = subprocess.run(("curl", "--help", "all"), capture_output=True, text=True, check=False).stdout
    if "--haproxy-clientip" not in help_text:
        pytest.skip("curl with --haproxy-clientip is required")
    context = procedural_context
    _plugin_log: Path

    def configure_origin(services: ServiceFactory) -> HttpBinServer:
        """Start the HTTPBin origin used by both proxy-protocol requests.

        :param services: Factory owning support services and their cleanup.
        """

        return services.httpbin("httpbin")

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable clear-text and TLS Proxy Protocol listeners and the test plugin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """
        nonlocal _plugin_log

        ats = ats_factory.create("ts", enable_tls=True, enable_proxy_protocol=True)
        plugin = context.runtime.resolve_artifact(
            context.test_directory,
            "{AtsBuildUraniumTestsDir}/pluginTest/tsapi/.libs/test_TSVConnPPInfo.so",
        )
        ats.copy_custom_plugin(plugin)
        ats.plugin_config.add_line(plugin.name)
        ats.remap_config.add_line(f"map /httpbin/ http://127.0.0.1:{_origin.port}/")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|proxyprotocol|test_TSVConnPPInfo",
            })
        _plugin_log = ats.log_directory / "test_TSVConnPPInfo_plugin_log.txt"
        ats.set_environment("OUTPUT_FILE", str(_plugin_log))
        return ats

    def verify_request(result: CommandResult) -> None:
        """Require curl and HTTPBin to complete the request.

        :param result: Completed command result to validate.
        """

        assert result.returncode == 0, result.output

    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)

    _origin.start()
    _ats.start()
    verify_request(
        curl.run((f"--haproxy-protocol --haproxy-clientip 1.2.3.4 "
                  f"'http://127.0.0.1:{_ats.proxy_protocol_port}/httpbin/get'"),))
    verify_request(
        curl.run(
            (
                f"--haproxy-protocol --haproxy-clientip 5.6.7.8 --insecure "
                f"'https://127.0.0.1:{_ats.proxy_protocol_https_port}/httpbin/get'"),))
    log = wait_for_file_lines(_plugin_log, r"PP Info Received", 2)
    assert log.startswith("Global: event=TS_EVENT_HTTP_SSN_START")
    assert re.search(r"PP Info Received:V1,P2,T1,SRC1\.2\.3\.4,DST(127\.0\.0\.1|1\.2\.3\.4)", log)
    assert re.search(r"PP Info Received:V1,P2,T1,SRC5\.6\.7\.8,DST(127\.0\.0\.1|5\.6\.7\.8)", log)
