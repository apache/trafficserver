#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

from collections.abc import Mapping, Sequence

import pytest

from tools.uranium.services import ATS, ATSFactory


class BadSetBodyScenario:
    """Reject ambiguous MIME arguments and unusable local response bodies."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Retain the ATS factory used for each independent startup failure.

        :param ats_factory: Factory for isolated ATS processes.
        """

        self._ats_factory = ats_factory

    def configure_failure(
        self,
        name: str,
        rule_lines: Sequence[str],
        error_marker: str,
        records: Mapping[str, int] | None = None,
        files: Mapping[str, str] | None = None,
    ) -> ATS:
        """Configure one invalid header_rewrite startup scenario.

        :param name: Unique scenario and ATS process name.
        :param rule_lines: Invalid header_rewrite rule lines.
        :param error_marker: Regular expression expected in diagnostics.
        :param records: Optional ATS record overrides.
        :param files: Optional inline configuration files.
        :return: ATS process configured to expect startup failure.
        """

        ats = self._ats_factory.create(name, enable_cache=False)
        if not ats.plugin_exists("header_rewrite.so"):
            pytest.skip("header_rewrite.so is required")
        if records:
            ats.records.update(records)
        if files:
            for filename, content in files.items():
                ats.write_config_file(filename, content)
        rule_name = f"{name}.conf"
        ats.write_config_file(rule_name, "\n".join(rule_lines) + "\n")
        ats.remap_config.add_line(
            f"map http://{name}.example.com/ http://127.0.0.1/ "
            f"@plugin=header_rewrite.so @pparam={rule_name}")
        ats.expect_start_failure(error_marker)
        return ats

    @staticmethod
    def verify_failure(ats: ATS) -> None:
        """Start ATS and ensure it never reaches full initialization.

        :param ats: ATS process configured for an expected startup failure.
        """

        ats.start()
        traffic_out = ats.traffic_out.read_text(errors="replace") if ats.traffic_out.exists() else ""
        assert "Traffic Server is fully initialized" not in traffic_out

    def run(self) -> None:
        """Exercise every invalid set-body configuration."""

        hook = "cond %{REMAP_PSEUDO_HOOK}"
        cases = (
            (
                "missing-body-file", [hook, "  set-body-from-file no-such-body.json application/json"],
                r"unable to load body file.*no-such-body\.json", None, None),
            (
                "oversized-body-file", [hook, "  set-body-from-file too-large-body.json application/json"],
                r"exceeds proxy\.config\.body_factory\.response_max_size.*8", {
                    "proxy.config.body_factory.response_max_size": 8
                }, {
                    "too-large-body.json": '{"error":"too large"}'
                }),
            ("ambiguous-body-arguments", [hook, "  set-body Sorry, page not found"], r"accepts at most two arguments", None, None),
            (
                "invalid-body-mime", [hook, "  set-body Sorry not-a-mime-type"], r"Content-Type must be a MIME type containing '/'",
                None, None),
        )
        for name, rules, marker, records, files in cases:
            self.verify_failure(self.configure_failure(name, rules, marker, records, files))


def test_header_rewrite_bad_set_body(ats_factory: ATSFactory) -> None:
    """Invalid set-body rules fail startup with a useful diagnostic.

    :param ats_factory: Factory for isolated ATS processes.
    """

    BadSetBodyScenario(ats_factory).run()
