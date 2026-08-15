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


class ShutdownScenario:
    """Verify explicit and signal-driven Traffic Server shutdown paths."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats_factory = ats_factory

    def configure_api_shutdown(self, kind: str) -> ATS:
        """Load the plugin that invokes TSFatal or TSEmergency."""

        ats = self._ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.exec_thread.autoconfig.enabled": 0,
                "proxy.config.exec_thread.autoconfig.scale": 1.5,
                "proxy.config.exec_thread.limit": 16,
                "proxy.config.accept_threads": 1,
                "proxy.config.task_threads": 2,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": f"TS{kind.title()}_test",
            })
        plugin = f"{kind}_shutdown.so"
        ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{plugin}")
        ats.plugin_config.add_line(plugin)
        return ats

    def run_api_shutdown(self, kind: str, return_code: int) -> None:
        """Verify an API-triggered shutdown exits with its documented status."""

        ats = self.configure_api_shutdown(kind)
        message = f"testing {kind} shutdown"
        ats.expect_start_failure(message, return_code)
        ats.start()
        assert not ats.is_running
        assert message in ats.diags_log.read_text(errors="replace")
        assert "failed to shutdown" not in ats.traffic_out.read_text(errors="replace")

    def run_clean_signal_shutdown(self) -> None:
        """Verify SIGTERM does not wake the crash-log helper spuriously."""

        ats = self._ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "server",
                "proxy.config.crash_log_helper": str(ats.run_directory / "bin/traffic_crashlog"),
            })
        ats.start()
        ats.stop()
        output = ats.traffic_out.read_text(errors="replace")
        assert "received exit signal, shutting down" in output
        assert "crashlog started" not in output
        assert "wrote crash log" not in output


def test_emergency_shutdown(ats_factory: ATSFactory) -> None:
    """TSEmergency terminates Traffic Server with EX_CONFIG."""

    ShutdownScenario(ats_factory).run_api_shutdown("emergency", 33)


def test_fatal_shutdown(ats_factory: ATSFactory) -> None:
    """TSFatal terminates Traffic Server with EX_SOFTWARE."""

    ShutdownScenario(ats_factory).run_api_shutdown("fatal", 70)


def test_crashlog_no_false_positive(ats_factory: ATSFactory) -> None:
    """A normal SIGTERM shutdown does not produce a false crash log."""

    ShutdownScenario(ats_factory).run_clean_signal_shutdown()
