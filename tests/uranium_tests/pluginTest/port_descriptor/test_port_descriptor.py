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

import socket

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines


def configure_ats(ats_factory: ATSFactory, *, _port: int) -> ATS:
    """Load the port-descriptor plugin with the allocated listener.

    :param ats_factory: Factory for an isolated Traffic Server instance.

    :param _port: Test-local port configured by the test.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.copy_custom_plugin("{AtsTestPluginsDir}/port_descriptor.so")
    ats.plugin_config.add_line(f"port_descriptor.so {_port}:ipv4")
    return ats


def connect(*, _port: int) -> None:
    """Open and close one connection to the plugin listener.

    :param _port: Test-local port configured by the test.
    """

    with socket.create_connection(("127.0.0.1", _port), timeout=10):
        pass


def test_port_descriptor(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The port-descriptor API accepts a connection on its listener.

    :param ats_factory: Factory for an isolated Traffic Server instance.
    :param services: Factory that allocates the plugin listener port.
    """

    _port = services.allocate_port()
    _ats = configure_ats(ats_factory, _port=_port)

    _ats.start()
    connect(_port=_port)
    diagnostics = wait_for_file_lines(
        _ats.diags_log,
        r"port_descriptor.*accepted connection",
        1,
        timeout=10,
    )
    assert "unexpected accept event" not in diagnostics
