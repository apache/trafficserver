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

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines


class BlockErrorsScenario:
    """Exercise the block_errors plugin's traffic_ctl message hook."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Load block_errors with its default configuration."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("block_errors.so"):
            pytest.skip("block_errors.so is required")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "block_errors",
        })
        ats.plugin_config.add_line("block_errors.so")
        return ats

    def send_message(self, tag: str, value: str, expected: str) -> str:
        """Send one plugin message and wait for its diagnostic."""

        result = self._ats.traffic_ctl("plugin", "msg", tag, value)
        assert result.returncode == 0, result.output
        assert self._ats.is_running
        return wait_for_file_lines(self._ats.traffic_out, expected, 1)

    def run(self) -> None:
        """Verify defaults, updates, unknown commands, and routing."""

        self._ats.start()
        diags = wait_for_file_lines(self._ats.diags_log, r"loading plugin.*block_errors\.so", 1)
        assert "block_errors.so" in diags
        defaults = "reset limit: 1000 per minute, timeout limit: 4 minutes, shutdown connection: 0 enabled: 1"
        wait_for_file_lines(self._ats.traffic_out, defaults, 1)

        output = self.send_message("block_errors.enabled", "0", "msg_hook: command=enabled data=0")
        assert "reset limit: 1000 per minute, timeout limit: 4 minutes, shutdown connection: 0 enabled: 0" in output
        output = self.send_message("block_errors.limit", "500", "msg_hook: command=limit data=500")
        assert "reset limit: 500 per minute" in output
        output = self.send_message("block_errors.cycles", "8", "msg_hook: command=cycles data=8")
        assert "timeout limit: 8 minutes" in output
        output = self.send_message("block_errors.shutdown", "1", "msg_hook: command=shutdown data=1")
        assert "shutdown connection: 1" in output
        self.send_message(
            "block_errors.unknown_command",
            "test",
            "msg_hook: unknown command 'unknown_command'",
        )
        self.send_message(
            "other_plugin.command",
            "test",
            "msg_hook: message for a different plugin: other_plugin",
        )


def test_block_errors(ats_factory: ATSFactory) -> None:
    """block_errors applies and routes runtime messages without restarting."""

    BlockErrorsScenario(ats_factory).run()
