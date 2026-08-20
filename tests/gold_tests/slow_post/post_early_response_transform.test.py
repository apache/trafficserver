"""Verify ATS does not crash when a server replies before receiving the full POST body and a request transform plugin is active.

When a POST request has a request transform and the origin responds before the
full body is forwarded through the transform chain, abort_tunnel() is called.
Without the fix, post_transform_info.entry is left stale in the vc_table,
causing a use-after-free in cleanup_all().
"""

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

import os
from ports import get_port
import sys

Test.Summary = __doc__


class PostEarlyResponseTransformTest:
    """Verify abort_tunnel with a request transform does not crash ATS."""

    _partial_post_client = 'partial_post_client.py'
    _quick_server = 'quick_server.py'
    _init_file = '__init__.py'
    _http_utils = 'http_utils.py'

    def __init__(self):
        """Configure and run the test."""
        tr = Test.AddTestRun('Partial POST with request transform and early server response')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> None:
        """Configure the DNS process.

        :param tr: The test run to associate the DNS process with.
        """
        self._dns = tr.MakeDNServer('dns', default='127.0.0.1')

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the quick-responding origin server.

        The server responds immediately after receiving the request headers,
        before the full POST body arrives.

        :param tr: The test run to associate the server process with.
        """
        server = tr.Processes.Process('server')
        server_port = get_port(server, 'http_port')
        server.Command = f'{sys.executable} {self._quick_server} 127.0.0.1 {server_port}'
        server.Ready = When.PortOpenv4(server_port)
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun') -> None:
        """Configure ATS with the null_transform_request plugin.

        :param tr: The test run to associate the ATS process with.
        """
        self._ts = tr.MakeATSProcess('ts')
        self._ts.Disk.remap_config.AddLine(f'map / http://quick.server.com:{self._server.Variables.http_port}')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'null_transform_request.so'), self._ts)

    def _configure_client(self, tr: 'TestRun') -> None:
        """Configure the partial POST client.

        Sends a POST with a large Content-Length but only a small body,
        triggering abort_tunnel when the origin responds early.

        :param tr: The test run to associate the client process with.
        """
        tools_dir = self._ts.Variables.AtsTestToolsDir
        http_utils = os.path.join(tools_dir, 'http_utils.py')
        tr.Setup.CopyAs(self._init_file, Test.RunDirectory)
        tr.Setup.CopyAs(http_utils, Test.RunDirectory)
        tr.Setup.CopyAs(self._quick_server, Test.RunDirectory)
        tr.Setup.CopyAs(self._partial_post_client, Test.RunDirectory)

        p = tr.Processes.Default
        p.Command = (f'{sys.executable} {self._partial_post_client} '
                     f'127.0.0.1 {self._ts.Variables.port}')
        p.ReturnCode = 0
        p.Streams.All += Testers.ContainsExpression('Got response', 'Verify client received a response from ATS')

        self._ts.StartBefore(self._dns)
        self._ts.StartBefore(self._server)
        p.StartBefore(self._ts)
        tr.Timeout = 10


PostEarlyResponseTransformTest()
