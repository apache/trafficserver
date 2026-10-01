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


def run_basic_conf_remap(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
    """Exercise valid and invalid conf_remap YAML overrides.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_yaml: Use yaml used by this test step.
    """

    __INVALID_RECORD = ("'proxy.config.plugin.dynamic_reload_mode' is not a configuration variable or cannot be overridden")

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the origin used by each successfully configured ATS.

        :param services: Factory owning support services and their cleanup.
        """

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

    def configure_ats(name: str, filename: str, content: str) -> ATS:
        """Create one ATS instance with a conf_remap override file.

        :param name: Unique service or case name within this test.
        :param filename: Filename used by this test step.
        :param content: Content used by this test step.
        """

        ats = ats_factory.create(name)
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
        if use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    "    from: {url: 'http://www.testexample.com/'}",
                    f"    to: {{url: 'http://127.0.0.1:{_origin.port}'}}",
                    "    plugins:",
                    "      - name: conf_remap.so",
                    "        params:",
                    f"          - {parameter}",
                ])
        else:
            ats.remap_config.add_line(
                f"map http://www.testexample.com/ http://127.0.0.1:{_origin.port} "
                f"@plugin=conf_remap.so @pparam={parameter}")
        return ats

    def run_success(name: str, filename: str, content: str, warning: str = "") -> None:
        """Start one valid configuration and verify it proxies a request.

        :param name: Unique service or case name within this test.
        :param filename: Filename used by this test step.
        :param content: Content used by this test step.
        :param warning: Warning used by this test step.
        """

        ats = configure_ats(name, filename, content)
        ats.start()
        result = curl.get(ats, "/test", headers={"Host": "www.testexample.com"}, options=f"--verbose")
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stderr, result.output
        if warning:
            assert warning in ats.diags_log.read_text(errors="replace")
        ats.stop()

    def run_failure(name: str, filename: str, content: str, diagnostic: str) -> None:
        """Start one invalid configuration and verify its fatal diagnostic.

        :param name: Unique service or case name within this test.
        :param filename: Filename used by this test step.
        :param content: Content used by this test step.
        :param diagnostic: Diagnostic used by this test step.
        """

        ats = configure_ats(name, filename, content)
        ats.expect_start_failure(diagnostic, 33)
        ats.start()

    _origin = configure_origin(services)

    _origin.start()
    run_success(
        "success",
        "testexample_remap.yaml",
        "records:\n  url_remap:\n    pristine_host_hdr: 1\n",
    )
    run_failure(
        "type-mismatch",
        "mismatch_field_type_remap.yaml",
        "records:\n  url_remap:\n    pristine_host_hdr: !!float '1'\n",
        "'proxy.config.url_remap.pristine_host_hdr' variable type mismatch",
    )
    run_failure(
        "invalid-record",
        "invalid_field_type_remap.yaml",
        "records:\n  plugin:\n    dynamic_reload_mode: 1\n",
        __INVALID_RECORD,
    )
    run_success(
        "mixed-records",
        "testexample2_remap.yaml",
        "records:\n  plugin:\n    dynamic_reload_mode: 1\n  url_remap:\n    pristine_host_hdr: 1\n",
        __INVALID_RECORD,
    )
    run_success(
        "null-value",
        "null_value_remap.yaml",
        'records:\n  url_remap:\n    pristine_host_hdr: 1\n  hostdb:\n    ip_resolve: "NULL"\n',
    )
