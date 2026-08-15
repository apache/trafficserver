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

from tools.uranium.services import ATS, ATSFactory


class PrefetchBadCountScenario:
    """Verify prefetch refuses a fetch count outside unsigned range."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a remap rule with an out-of-range fetch count."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("prefetch.so"):
            pytest.skip("prefetch.so is not installed")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "prefetch",
        })
        ats.remap_config.add_line(
            "map http://domain.in http://127.0.0.1:8080 @plugin=prefetch.so "
            "@pparam=--front=true @pparam=--fetch-policy=simple @pparam=--fetch-count=5000000000")
        ats.expect_start_failure("invalid --fetch-count '5000000000'", 33)
        return ats

    def run(self) -> None:
        """Start ATS and observe the expected remap load failure."""

        self._ats.start()
        assert not self._ats.is_running


def test_prefetch_bad_count_refused(ats_factory: ATSFactory) -> None:
    """An out-of-range prefetch count fails remap configuration loading."""

    PrefetchBadCountScenario(ats_factory).run()
