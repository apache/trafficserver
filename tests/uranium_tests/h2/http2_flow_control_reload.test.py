"""Verify HTTP/2 flow control policies change without restarting ATS."""

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

import sys
from ports import get_port

Test.Summary = __doc__


class FlowControlReloadTest:
    """Observe both connection receive windows across runtime policy changes."""

    def __init__(self) -> None:
        self._ts = self._configure_trafficserver()
        self._configure_probe()

    def _configure_trafficserver(self) -> 'Process':
        ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        ts.addDefaultSSLFiles()
        get_port(ts, 'origin_port')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.ssl.client.alpn_protocols': 'h2',
                'proxy.config.http2.initial_window_size_in': 65535,
                'proxy.config.http2.initial_window_size_out': 65535,
                'proxy.config.http2.max_concurrent_streams_in': 100,
                'proxy.config.http2.max_concurrent_streams_out': 100,
                'proxy.config.http2.flow_control.policy_in': 0,
                'proxy.config.http2.flow_control.policy_out': 0,
            })
        ts.Disk.ssl_multicert_yaml.AddLines(
            ['ssl_multicert:', '  - dest_ip: "*"', '    ssl_cert_name: server.pem', '    ssl_key_name: server.key'])
        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{ts.Variables.origin_port}')
        return ts

    def _configure_probe(self) -> None:
        tr = Test.AddTestRun('Reload inbound and outbound flow control policies on the same ATS process')
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.Command = (
            f'{sys.executable} {Test.TestDirectory}/http2_flow_control_reload.py '
            f'--https-port {self._ts.Variables.ssl_port} --http-port {self._ts.Variables.port} '
            f'--origin-port {self._ts.Variables.origin_port} '
            f'--cert {self._ts.Variables.SSLDir}/server.pem --key {self._ts.Variables.SSLDir}/server.key')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 180
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            'PASS: both flow control policies reloaded', 'Both policies must affect new connections without a restart')
        tr.StillRunningAfter = self._ts


FlowControlReloadTest()
