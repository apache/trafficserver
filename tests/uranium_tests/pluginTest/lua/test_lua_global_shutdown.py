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
import signal

from tools.uranium.services import ATSFactory, Curl, OriginServer, ServiceFactory


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


def test_lua_global_shutdown(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Invoke the global request and shutdown hooks for the configured states.

    :param ats_factory: Factory for an isolated Traffic Server instance.
    :param services: Factory owning the origin and its cleanup.
    :param curl: Transport-aware client used to trigger the request hook.
    """
    origin = configure_origin(services)
    ats = ats_factory.create("ats")
    if not ats.plugin_exists("tslua.so"):
        pytest.skip("tslua.so is required")
    ats.copy_to_config("global_shutdown.lua")
    ats.plugin_config.add_line(f"tslua.so --states=2 {ats.config_directory}/global_shutdown.lua")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ts_lua",
    })
    origin.start()
    ats.start()
    response = curl.get(ats, headers={"Host": "www.example.com"})
    assert response.returncode == 0, response.output
    ats.send_signal(signal.SIGTERM)
    ats.wait(timeout=10)
    output = ats.traffic_out.read_text(errors="replace")
    assert "do_global_read_request called" in output, output
    assert output.count("__shutdown__ called") == 2, output
