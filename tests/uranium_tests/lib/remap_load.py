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


def run_remap_load(ats_factory: ATSFactory, *, use_yaml: bool, file_exists: bool, should_start: bool) -> None:
    """Verify the minimum-rule policy for an empty or missing remap file.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param use_yaml: Use yaml used by this test step.
    :param file_exists: File exists used by this test step.
    :param should_start: Should start used by this test step.
    """

    def configure_ats() -> ATS:
        """Stage the selected remap file state and minimum-rule policy."""

        ats = ats_factory.create("ts")
        ats.records.update({"proxy.config.url_remap.min_rules_required": 0 if should_start else 1})
        filename = "remap.yaml" if use_yaml else "remap.config"
        if file_exists:
            (ats.remap_yaml if use_yaml else ats.remap_config).add_line("")
        else:
            ats.omit_config_file(filename)
        if not should_start:
            ats.expect_start_failure(r"remap\.(?:yaml|config) failed to load")
        return ats

    ats = configure_ats()
    ats.start()
    assert ats.is_running is should_start
