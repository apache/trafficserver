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

from tools.uranium.services import ATS, ATSFactory


class CppApiScenario:
    """Run the C++ plugin API self-tests during plugin initialization."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install and load the test_cppapi test plugin."""

        ats = ats_factory.create("ts")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/test_cppapi.so")
        ats.plugin_config.add_line("test_cppapi.so")
        return ats

    def run(self) -> None:
        """Start ATS and leave plugin diagnostics to fixture validation."""

        self._ats.start()
        assert self._ats.is_running


def test_cppapi(ats_factory: ATSFactory) -> None:
    """The test_cppapi plugin initializes without a failed self-test."""

    CppApiScenario(ats_factory).run()
