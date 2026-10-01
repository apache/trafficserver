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

import time

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Load test_log_interface with its initialization-write mode.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update({"proxy.config.log.log_buffer_size": 9216})
    ats.copy_custom_plugin("{AtsTestPluginsDir}/test_log_interface.so")
    ats.plugin_config.add_line("test_log_interface.so --write-during-init")
    return ats


def test_log_plugin_init(ats_factory: ATSFactory) -> None:
    """Plugin initialization text reaches its configured log file.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    plugin_log = _ats.log_directory / "test_log_interface.log"
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if plugin_log.exists() and "Writing during plugin initialization" in plugin_log.read_text(errors="replace"):
            return
        time.sleep(0.1)
    raise AssertionError(f"Plugin initialization text was not flushed to {plugin_log}")
