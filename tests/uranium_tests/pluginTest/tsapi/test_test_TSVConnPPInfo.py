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


class TSVConnProxyProtocolInfoScenario:
    """Read Proxy Protocol metadata through the TSVConnPPInfo API."""

    def __init__(
        self,
        context: ProceduralContext,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
    ) -> None:
        self._context = context
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> HttpBinServer:
        """Start the HTTPBin origin used by both proxy-protocol requests."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable clear-text and TLS Proxy Protocol listeners and the test plugin."""

        ats = ats_factory.create("ts", enable_tls=True, enable_proxy_protocol=True)
        plugin = self._context.runtime.resolve_artifact(
            self._context.test_directory,
            "{AtsBuildUraniumTestsDir}/pluginTest/tsapi/.libs/test_TSVConnPPInfo.so",
        )
        ats.copy_custom_plugin(plugin)
        ats.plugin_config.add_line(plugin.name)
        ats.remap_config.add_line(f"map /httpbin/ http://127.0.0.1:{self._origin.port}/")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|proxyprotocol|test_TSVConnPPInfo",
            })
        self._plugin_log = ats.log_directory / "test_TSVConnPPInfo_plugin_log.txt"
        ats.set_environment("OUTPUT_FILE", str(self._plugin_log))
        return ats

    @staticmethod
    def verify_request(result: CommandResult) -> None:
        """Require curl and HTTPBin to complete the request."""

        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Issue clear-text and TLS requests with distinct client addresses."""

        self._origin.start()
        self._ats.start()
        self.verify_request(
            self._curl.run(
                (
                    f"--haproxy-protocol --haproxy-clientip 1.2.3.4 "
                    f"'http://127.0.0.1:{self._ats.proxy_protocol_port}/httpbin/get'"),))
        self.verify_request(
            self._curl.run(
                (
                    f"--haproxy-protocol --haproxy-clientip 5.6.7.8 --insecure "
                    f"'https://127.0.0.1:{self._ats.proxy_protocol_https_port}/httpbin/get'"),))
        log = wait_for_file_lines(self._plugin_log, r"PP Info Received", 2)
        assert log.startswith("Global: event=TS_EVENT_HTTP_SSN_START")
        assert re.search(r"PP Info Received:V1,P2,T1,SRC1\.2\.3\.4,DST(127\.0\.0\.1|1\.2\.3\.4)", log)
        assert re.search(r"PP Info Received:V1,P2,T1,SRC5\.6\.7\.8,DST(127\.0\.0\.1|5\.6\.7\.8)", log)


def test_test_TSVConnPPInfo(
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """The TSVConn API reports Proxy Protocol version, transport, and addresses."""

    help_text = subprocess.run(("curl", "--help", "all"), capture_output=True, text=True, check=False).stdout
    if "--haproxy-clientip" not in help_text:
        pytest.skip("curl with --haproxy-clientip is required")
    TSVConnProxyProtocolInfoScenario(procedural_context, ats_factory, services, curl).run()
