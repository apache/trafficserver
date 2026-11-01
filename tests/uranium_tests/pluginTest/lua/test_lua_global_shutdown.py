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
from pathlib import Path
import signal
import sys

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin used to prove the global plugin is active.

    :param services: Factory owning support services and their cleanup.
    """

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


def configure_ats(ats_factory: ATSFactory, name: str, *, busy: bool, _origin: OriginServer) -> ATS:
    """Load global and remap Lua states for shutdown-barrier coverage.

    :param ats_factory: Factory owning both Traffic Server instances.
    :param name: Unique process identifier used by the signal helper.
    :param busy: Whether to enable the Lua state handshake during load.

    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create(name)
    if not ats.plugin_exists("tslua.so"):
        pytest.skip("tslua.so is required")
    ats.copy_to_config("global_shutdown.lua", "remap_shutdown.lua")
    if busy:
        ats.set_environment("TS_LUA_SHUTDOWN_TEST_DIR", str(ats.run_directory))
    ats.plugin_config.add_line(f"tslua.so --states=2 {ats.config_directory}/global_shutdown.lua")
    ats.remap_config.add_line(
        f"map http://remap.example.com/ http://127.0.0.1:{_origin.port}/ "
        f"@plugin=tslua.so @pparam=--states=1 @pparam={ats.config_directory}/remap_shutdown.lua")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
            "proxy.config.stop.shutdown_timeout": 0,
            "proxy.config.plugin.dynamic_reload_mode": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ts_lua",
        })
    return ats


def test_lua_global_shutdown(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Lua invokes __shutdown__ once for every configured global state.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, "lua_shutdown_ts", busy=True, _origin=_origin)
    _quiet = configure_ats(ats_factory, "lua_shutdown_quiet_ts", busy=False, _origin=_origin)

    _origin.start()
    _ats.start()
    response = curl.get(_ats, headers={"Host": "www.example.com"})
    assert response.returncode == 0, response.output
    for ats in (_ats, _quiet):
        if ats is _quiet:
            ats.start()
        response = curl.get(ats, "/remap-hello", headers={"Host": "remap.example.com"})
        assert response.returncode == 0, response.output
        assert "Remap Lua response" in response.stdout, response.output
        if ats is _ats:
            directory = Path(__file__).parent
            client = services.process(
                "shutdown-race",
                (
                    sys.executable, directory / "shutdown_race_client.py", "127.0.0.1", str(ats.http_port), str(
                        ats.run_directory), ats.name),
                environment={"PYTHONPATH": str(directory.parents[1] / "logging")},
            )
            result = client.run(timeout=45)
            assert result.returncode == 0, result.output
        else:
            ats.send_signal(signal.SIGTERM)
        ats.wait(timeout=10)
        output = ats.traffic_out.read_text(errors="replace")
        assert "shutdown barrier acquired" in output
        assert "__shutdown__ called for state 0" in output
        assert "do_remap called" in output
        for forbidden in ("__shutdown__ overlapped an active Lua state", "do_global_read_request ran after __shutdown__",
                          "do_remap ran after __shutdown__"):
            assert forbidden not in output, output
        assert "skipping __shutdown__" not in ats.diags_log.read_text(errors="replace")
    output = _ats.traffic_out.read_text(errors="replace")
    assert "do_global_read_request called" in output
    assert "__shutdown__ called for state 1" in output
    assert output.count("__shutdown__ called for state") == 2
