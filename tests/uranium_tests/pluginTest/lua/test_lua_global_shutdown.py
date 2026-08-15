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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class LuaGlobalShutdownScenario:
    """Exercise Lua global request and shutdown lifecycle callbacks."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the origin used to prove the global plugin is active."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load two Lua states with a global lifecycle script."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("tslua.so"):
            pytest.skip("tslua.so is required")
        ats.copy_to_config("global_shutdown.lua")
        ats.plugin_config.add_line(f"tslua.so --states=2 {ats.config_directory}/global_shutdown.lua")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ts_lua",
        })
        return ats

    def run(self) -> None:
        """Send one request, stop ATS, and verify both lifecycle hooks."""

        self._origin.start()
        self._ats.start()
        response = self._curl.get(self._ats, headers={"Host": "www.example.com"})
        assert response.returncode == 0, response.output
        self._ats.stop()
        output = self._ats.traffic_out.read_text(errors="replace")
        assert "do_global_read_request called" in output
        assert output.count("__shutdown__ called") == 2


def test_lua_global_shutdown(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Lua invokes __shutdown__ once for every configured global state."""

    LuaGlobalShutdownScenario(ats_factory, services, curl).run()
