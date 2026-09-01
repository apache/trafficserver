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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, ServiceFactory


class RemapIncludeScenario:
    """Verify a remap.config include is reread during configuration reload."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Provide deterministic resolution for the initial remap targets."""

        return services.dns("dns", default="127.0.0.1")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a remap file whose middle rule comes from test.inc."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.write_config_file(
            "test.inc",
            "map http://example.two/ http://yada.com/ "
            "@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1\n",
        )
        ats.remap_config.add_lines(
            (
                "map http://example.one/ http://yada.com/",
                ".include test.inc",
                "map http://example.three/ http://yada.com/",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "regex_remap|url_rewrite|plugin_factory",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        return ats

    def update_include_and_reload(self) -> None:
        """Replace the included mapping and wait for the reload to complete."""

        include = self._ats.config_directory / "test.inc"
        include.write_text("map http://example.four/ http://localhost/ @plugin=generator.so\n")
        result = self._ats.traffic_ctl(
            "config",
            "reload",
            "--monitor",
            "--token",
            "remap-include",
            "--initial-wait",
            "0.2",
            "--refresh-int",
            "0.1",
            "--timeout",
            "30s",
        )
        assert result.returncode == 0, result.output

    def verify_new_mapping(self) -> None:
        """Verify the new include rule reaches the generator plugin."""

        result = self._curl.get(self._ats, "/nocache/5", headers={"Host": "example.four"})
        assert result.returncode == 0, result.output
        assert "xxxxx" in result.output
        assert "xxxxxx" not in result.output

    def run(self) -> None:
        """Start the services, reload the include, and request its new mapping."""

        self._dns.start()
        self._ats.start()
        self.update_include_and_reload()
        self.verify_new_mapping()


def test_remap_inc(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A remap include update is visible after traffic_ctl config reload."""

    RemapIncludeScenario(ats_factory, services, curl).run()
