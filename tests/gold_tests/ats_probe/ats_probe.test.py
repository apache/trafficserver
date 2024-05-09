'''Verify SystemTap ATS probe behavior.'''
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

Test.Summary = '''Verify SystemTap ATS probes.'''

# Skipping this test generally because it requires privilege. Thus most CI
# systems will fail this test.  Comment out the following line to run in your
# privileged environment.
Test.SkipUnless(Condition(lambda: False, "Test requires privilege", True))
Test.SkipUnless(Condition.HasProgram("bpftrace", "Need bpftrace to verify the probe."))


class TestATSProbe:
    '''Verify SystemTap ATS probes.'''
    replay_file: str = 'ats_probe.replay.yaml'
    bt_script: str = 'ats_probe.bt'

    def __init__(self):
        '''Configure the TestRun.'''
        tr = Test.AddTestRun('Verify ATS probes.')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_bpftrace(tr)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        '''Configure the DNS process.

        :param tr: The TestRun to add the DNS process to.
        :return: The DNS process.
        '''
        dns = tr.MakeDNServer('dns', default='127.0.0.1')
        self._dns = dns
        return dns

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        '''Configure the server process.

        :param tr: The TestRun to add the server process to.
        :return: The server process.
        '''
        server = tr.AddVerifierServerProcess('server', self.replay_file)
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        '''Configure the Traffic Server process.

        :param tr: The TestRun to add the Traffic Server process to.
        :return: The Traffic Server process.
        '''
        ts = tr.MakeATSProcess("ts", enable_cache=False)
        self._ts = ts

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL'
            })
        server_port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map / http://backend.server.com:{server_port}')
        return ts

    def _configure_bpftrace(self, tr: 'TestRun') -> 'Process':
        '''Configure the bpftrace process for the ATS probe.

        :param tr: The TestRun to add the bpftrace process to.
        :return: The bpftrace process.
        '''
        bpftrace = tr.Processes.Process('bpftrace')
        self._bpftrace = bpftrace
        tr.Setup.Copy(self.bt_script)
        tr_script = os.path.join(tr.RunDirectory, self.bt_script)
        bpftrace.Command = f'sudo bpftrace {tr_script}'
        bpftrace.ReturnCode = 0
        bpftrace.Streams.All += Testers.ContainsExpression(
            'backend.server.com', 'The probe correctly printed the origin servername.')
        return bpftrace

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure the client process.

        :param tr: The TestRun to add the client process to.
        :return: The client process.
        '''
        client = tr.AddVerifierClientProcess("client", self.replay_file, http_ports=[self._ts.Variables.port])
        self._ts.StartBefore(self._dns)
        self._ts.StartBefore(self._server)
        self._bpftrace.StartBefore(self._ts)
        client.StartBefore(self._bpftrace)


TestATSProbe()
