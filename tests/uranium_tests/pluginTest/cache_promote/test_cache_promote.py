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

import pytest

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ProceduralContext, ServiceFactory


class CachePromoteScenario:
    """Exercise cache promotion before and after a followed redirect."""

    def __init__(
        self,
        context: ProceduralContext,
        ats_factory: ATSFactory,
        services: ServiceFactory,
    ) -> None:
        self._context = context
        self._ats_factory = ats_factory
        self._services = services
        self._directory = Path(__file__).parent
        plugin = Path(context.runtime.layout["PLUGINDIR"]) / "cache_promote.so"
        if not plugin.is_file():
            pytest.skip("cache_promote.so is required")
        self._origin = self.configure_origin()
        self._ats = self.configure_ats()

    def configure_origin(self) -> HttpBinServer:
        """Create the redirect-capable go-httpbin origin."""

        return self._services.httpbin("httpbin")

    def configure_ats(self) -> ATS:
        """Configure both cache_promote remap policies."""

        ats = self._ats_factory.create("ts", enable_cache=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|cache_promote",
                "proxy.config.http.number_of_redirections": 1,
                "proxy.config.http.redirect.actions": "self:follow",
            })
        ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key")
        ats.remap_config.add_line(
            f"map /test_0/ http://127.0.0.1:{self._origin.port}/ "
            "@plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000")
        ats.remap_config.add_line(
            f"map /test_1/ http://127.0.0.1:{self._origin.port}/ "
            "@plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000 "
            "@pparam=--disable-on-redirect @plugin=cachekey.so @pparam=--static-prefix=trafficserver.apache.org/443")
        return ats

    def configure_client(self) -> Path:
        """Render the Proxy Verifier replay with the selected origin port."""

        template = (self._directory / "replay/cache_promote.replay.yaml.tmpl").read_text()
        replay = self._context.run_directory / "cache_promote.replay.yaml"
        replay.write_text(template.format(httpbin_port=self._origin.port))
        return replay

    def run(self) -> None:
        """Start the services and execute the replay client."""

        replay = self.configure_client()
        client = self._services.verifier_client("verifier-client", replay, http_ports=[self._ats.http_port])
        self._origin.start()
        self._ats.start()
        client.run()


def test_cache_promote(
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
) -> None:
    """cache_promote applies its hit policy and redirect option."""

    CachePromoteScenario(procedural_context, ats_factory, services).run()
