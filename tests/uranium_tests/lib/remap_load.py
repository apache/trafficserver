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
"""Shared native scenario for remap configuration startup policy tests."""

from tools.uranium.services import ATS, ATSFactory


class RemapLoadScenario:
    """Verify the minimum-rule policy for an empty or missing remap file."""

    def __init__(self, ats_factory: ATSFactory, *, use_yaml: bool, file_exists: bool, should_start: bool) -> None:
        self._ats_factory = ats_factory
        self._use_yaml = use_yaml
        self._file_exists = file_exists
        self._should_start = should_start

    def configure_ats(self) -> ATS:
        """Stage the selected remap file state and minimum-rule policy."""

        ats = self._ats_factory.create("ts")
        ats.records.update({"proxy.config.url_remap.min_rules_required": 0 if self._should_start else 1})
        filename = "remap.yaml" if self._use_yaml else "remap.config"
        if self._file_exists:
            (ats.remap_yaml if self._use_yaml else ats.remap_config).add_line("")
        else:
            ats.omit_config_file(filename)
        if not self._should_start:
            ats.expect_start_failure(r"remap\.(?:yaml|config) failed to load")
        return ats

    def run(self) -> None:
        """Start ATS and assert whether the configured state is accepted."""

        ats = self.configure_ats()
        ats.start()
        assert ats.is_running is self._should_start
