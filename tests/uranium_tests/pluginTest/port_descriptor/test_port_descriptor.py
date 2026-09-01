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


class PortDescriptorScenario:
    """Verify a plugin can accept connections on a parsed port descriptor."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        """Allocate the plugin listener and configure Traffic Server.

        :param ats_factory: Factory for an isolated Traffic Server instance.
        :param services: Factory that allocates the plugin listener port.
        """

        self._port = services.allocate_port()
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the port-descriptor plugin with the allocated listener.

        :param ats_factory: Factory for an isolated Traffic Server instance.
        """

        ats = ats_factory.create("ts", enable_cache=False)
        ats.copy_custom_plugin("{AtsTestPluginsDir}/port_descriptor.so")
        ats.plugin_config.add_line(f"port_descriptor.so {self._port}:ipv4")
        return ats

    def connect(self) -> None:
        """Open and close one connection to the plugin listener."""

        with socket.create_connection(("127.0.0.1", self._port), timeout=10):
            pass

    def run(self) -> None:
        """Connect to the plugin port and verify the accept callback."""

        self._ats.start()
        self.connect()
        diagnostics = wait_for_file_lines(
            self._ats.diags_log,
            r"port_descriptor.*accepted connection",
            1,
            timeout=10,
        )
        assert "unexpected accept event" not in diagnostics


def test_port_descriptor(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The port-descriptor API accepts a connection on its listener.

    :param ats_factory: Factory for an isolated Traffic Server instance.
    :param services: Factory that allocates the plugin listener port.
    """

    PortDescriptorScenario(ats_factory, services).run()
