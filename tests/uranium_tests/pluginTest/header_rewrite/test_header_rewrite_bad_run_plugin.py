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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class BadRunPluginScenario:
    """Reject failed run-plugin initialization at startup and during reload."""

    ERROR_MARKER = "run-plugin unable to load"
    BAD_RULE = """\
cond %{REMAP_PSEUDO_HOOK}
  run-plugin conf_remap.so no_such_conf_remap_file.yaml
"""
    NESTED_BAD_RULE = """\
cond %{REMAP_PSEUDO_HOOK}
  if
    cond %{TRUE}
      run-plugin conf_remap.so no_such_conf_remap_file.yaml
  endif
"""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_reload_ats()

    def require_plugins(self) -> None:
        """Skip when either installed plugin needed by the scenario is absent."""

        if not self._ats.plugin_exists("header_rewrite.so") or not self._ats.plugin_exists("conf_remap.so"):
            pytest.skip("header_rewrite.so and conf_remap.so are required")

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the origin used before and after the rejected reload."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: reload.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_startup_ats(self) -> ATS:
        """Configure an invalid top-level run-plugin rule."""

        ats = self._ats_factory.create("ts-startup", enable_cache=False)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "header_rewrite",
        })
        ats.write_config_file("bad_run_plugin.conf", self.BAD_RULE)
        ats.remap_config.add_line(
            "map http://startup.example.com/ http://127.0.0.1/ "
            "@plugin=header_rewrite.so @pparam=bad_run_plugin.conf")
        ats.expect_start_failure(self.ERROR_MARKER)
        return ats

    def configure_reload_ats(self) -> ATS:
        """Configure the valid remap generation used by the live server."""

        ats = self._ats_factory.create("ts-reload", enable_cache=False)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "header_rewrite",
        })
        ats.write_config_file("nested_bad_run_plugin.conf", self.NESTED_BAD_RULE)
        ats.remap_config.add_line(f"map http://reload.example.com http://127.0.0.1:{self._origin.port}")
        return ats

    def verify_startup_rejection(self) -> None:
        """Confirm invalid startup configuration exits cleanly rather than aborting."""

        ats = self.configure_startup_ats()
        ats.start()
        assert self.ERROR_MARKER in ats.diags_log.read_text(errors="replace")
        assert "Traffic Server is fully initialized" not in ats.traffic_out.read_text(errors="replace")

    def request(self) -> None:
        """Verify the currently active remap generation still serves traffic."""

        result = self._curl.get(self._ats, headers={"Host": "reload.example.com"}, options=("--verbose",))
        assert result.returncode == 0, result.output
        assert "200 OK" in result.stderr

    def install_invalid_remap(self) -> None:
        """Replace remap.config with a nested run-plugin whose instance cannot load."""

        self._ats.remap_config.path.write_text(
            f"map http://reload.example.com http://127.0.0.1:{self._origin.port} "
            "@plugin=header_rewrite.so @pparam=nested_bad_run_plugin.conf\n")

    def reject_reload(self) -> None:
        """Reload the invalid table and wait until ATS reports the failure."""

        token = "bad-run-plugin"
        result = self._ats.traffic_ctl("config", "reload", "--token", token)
        assert result.returncode == 0, result.output
        deadline = time.monotonic() + 15
        latest = ""
        while time.monotonic() < deadline:
            status = self._ats.traffic_ctl("config", "status", "--token", token)
            latest = status.output.lower()
            if "failed" in latest:
                return
            if "success" in latest:
                break
            time.sleep(0.1)
        raise AssertionError(f"Invalid remap reload was not rejected:\n{latest}")

    def run(self) -> None:
        """Exercise startup rejection and atomic live-reload rejection."""

        self.require_plugins()
        self.verify_startup_rejection()
        self._origin.start()
        self._ats.start()
        self.request()
        self.install_invalid_remap()
        self.reject_reload()
        self.request()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.ERROR_MARKER in self._ats.diags_log.read_text(errors="replace"):
                return
            time.sleep(0.1)
        raise AssertionError("The rejected reload did not log the run-plugin initialization failure")


def test_header_rewrite_bad_run_plugin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Failed run-plugin initialization is rejected without losing the prior remap generation."""

    BadRunPluginScenario(ats_factory, services, curl).run()
