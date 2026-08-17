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

import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory


class ConfigDestroyThreadScenario:
    """Wait for replaced configurations to be released on ET_TASK."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the lifecycle scenario.

        :param ats_factory: Factory that owns the ATS instance.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable configuration lifecycle diagnostics.

        :param ats_factory: Factory that owns the ATS instance.
        """

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "config",
        })
        ats.remap_config.add_line("map / http://127.0.0.1:8080")
        return ats

    def replace_configurations(self) -> None:
        """Replace parent and HTTP configurations through different threads."""

        self._ats.parent_config.path.touch()
        reload_result = self._ats.traffic_ctl(
            "config",
            "reload",
            "-m",
            "-t",
            "config_destroy_thread",
            "-w",
            "1",
            "-r",
            "0.5",
            "-T",
            "30s",
        )
        assert reload_result.returncode == 0, reload_result.output
        set_result = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.http.response_server_str",
            "probe",
        )
        assert set_result.returncode == 0, set_result.output

    def verify_release_thread(self) -> None:
        """Require ET_TASK destruction and reject ET_NET destruction."""

        time.sleep(80)
        output = self._ats.traffic_out.read_text(errors="replace")
        assert re.search(r"Destroyed config \d+ in \d+ ns on thread \[ET_TASK", output), output
        assert re.search(r"Destroyed config \d+ in \d+ ns on thread \[ET_NET", output) is None, output

    def run(self) -> None:
        """Replace configurations and inspect their delayed destruction."""

        self._ats.start()
        self.replace_configurations()
        self.verify_release_thread()


@pytest.mark.manual(reason="takes over 60 seconds")
def test_config_destroy_thread(ats_factory: ATSFactory) -> None:
    """Replaced configurations are destroyed on an ET_TASK thread.

    :param ats_factory: Factory that owns the ATS instance.
    """

    ConfigDestroyThreadScenario(ats_factory).run()
