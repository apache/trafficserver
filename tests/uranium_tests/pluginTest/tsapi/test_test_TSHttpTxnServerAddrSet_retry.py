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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, ProceduralContext, ServiceFactory, wait_for_file_lines


class ServerAddrSetRetryScenario:
    """Select retry addresses through TSHttpTxnServerAddrSet from the OS_DNS hook."""

    def __init__(
        self,
        enable_cache: bool,
        context: ProceduralContext,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
    ) -> None:
        self._enable_cache = enable_cache
        self._curl = curl
        self._bogus_port = services.allocate_port()
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(context, ats_factory)

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve every name to loopback for deterministic retries."""

        return services.dns("dns", default="127.0.0.1")

    def configure_ats(self, context: ProceduralContext, ats_factory: ATSFactory) -> ATS:
        """Load the test plugin and permit several connection retries."""

        ats = ats_factory.create("ts", enable_cache=self._enable_cache)
        plugin = context.runtime.resolve_artifact(
            context.test_directory,
            "{AtsBuildUraniumTestsDir}/pluginTest/tsapi/.libs/test_TSHttpTxnServerAddrSet_retry.so",
        )
        ats.copy_custom_plugin(plugin)
        ats.plugin_config.add_line(plugin.name)
        ats.records.update(
            {
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|test_TSHttpTxnServerAddrSet_retry",
                "proxy.config.http.connect_attempts_max_retries": 3,
                "proxy.config.http.connect_attempts_timeout": 1,
            })
        ats.remap_config.add_line(f"map / http://non.existent.server.com:{self._bogus_port}")
        return ats

    def run(self) -> None:
        """Issue a failing request and verify the plugin supplied retry addresses."""

        self._dns.start()
        self._ats.start()
        self._curl.get(
            self._ats,
            "/",
            options=f"--silent --verbose --connect-timeout 5 --output /dev/null",
            timeout=15,
        )
        diags = wait_for_file_lines(self._ats.diags_log, "SUCCESS: OS_DNS hook was called", 1)
        assert "OS_DNS hook called, count=1" in diags
        if self._enable_cache:
            traffic_out = self._ats.traffic_out.read_text(errors="replace")
            assert "failed assertion" not in traffic_out
            assert "received signal 6" not in traffic_out


@pytest.mark.parametrize("enable_cache", (False, True), ids=("no-cache", "cache"))
def test_test_TSHttpTxnServerAddrSet_retry(
    enable_cache: bool,
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """TSHttpTxnServerAddrSet retries safely with and without cache enabled."""

    ServerAddrSetRetryScenario(enable_cache, procedural_context, ats_factory, services, curl).run()
