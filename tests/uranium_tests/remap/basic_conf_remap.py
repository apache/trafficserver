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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class BasicConfRemapScenario:
    """Exercise valid and invalid conf_remap YAML overrides."""

    _INVALID_RECORD = ("'proxy.config.plugin.dynamic_reload_mode' is not a configuration variable or cannot be overridden")

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
        self._ats_factory = ats_factory
        self._curl = curl
        self._use_yaml = use_yaml
        self._origin = self.configure_origin(services)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the origin used by each successfully configured ATS."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /test HTTP/1.1\r\nHost: www.testexample.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, name: str, filename: str, content: str) -> ATS:
        """Create one ATS instance with a conf_remap override file."""

        ats = self._ats_factory.create(name)
        if not ats.plugin_exists("conf_remap.so"):
            pytest.skip("conf_remap.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "conf_remap",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.referer_filter": 1,
                "proxy.config.url_remap.pristine_host_hdr": 0,
            })
        ats.write_config_file(filename, content)
        parameter = ats.config_directory / filename
        if self._use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    "    from: {url: 'http://www.testexample.com/'}",
                    f"    to: {{url: 'http://127.0.0.1:{self._origin.port}'}}",
                    "    plugins:",
                    "      - name: conf_remap.so",
                    "        params:",
                    f"          - {parameter}",
                ])
        else:
            ats.remap_config.add_line(
                f"map http://www.testexample.com/ http://127.0.0.1:{self._origin.port} "
                f"@plugin=conf_remap.so @pparam={parameter}")
        return ats

    def run_success(self, name: str, filename: str, content: str, warning: str = "") -> None:
        """Start one valid configuration and verify it proxies a request."""

        ats = self.configure_ats(name, filename, content)
        ats.start()
        result = self._curl.get(ats, "/test", headers={"Host": "www.testexample.com"}, options=("--verbose",))
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stderr, result.output
        if warning:
            assert warning in ats.diags_log.read_text(errors="replace")
        ats.stop()

    def run_failure(self, name: str, filename: str, content: str, diagnostic: str) -> None:
        """Start one invalid configuration and verify its fatal diagnostic."""

        ats = self.configure_ats(name, filename, content)
        ats.expect_start_failure(diagnostic, 33)
        ats.start()

    def run(self) -> None:
        """Run the complete conf_remap validation matrix."""

        self._origin.start()
        self.run_success(
            "success",
            "testexample_remap.yaml",
            "records:\n  url_remap:\n    pristine_host_hdr: 1\n",
        )
        self.run_failure(
            "type-mismatch",
            "mismatch_field_type_remap.yaml",
            "records:\n  url_remap:\n    pristine_host_hdr: !!float '1'\n",
            "'proxy.config.url_remap.pristine_host_hdr' variable type mismatch",
        )
        self.run_failure(
            "invalid-record",
            "invalid_field_type_remap.yaml",
            "records:\n  plugin:\n    dynamic_reload_mode: 1\n",
            self._INVALID_RECORD,
        )
        self.run_success(
            "mixed-records",
            "testexample2_remap.yaml",
            "records:\n  plugin:\n    dynamic_reload_mode: 1\n  url_remap:\n    pristine_host_hdr: 1\n",
            self._INVALID_RECORD,
        )
        self.run_success(
            "null-value",
            "null_value_remap.yaml",
            'records:\n  url_remap:\n    pristine_host_hdr: 1\n  hostdb:\n    ip_resolve: "NULL"\n',
        )
