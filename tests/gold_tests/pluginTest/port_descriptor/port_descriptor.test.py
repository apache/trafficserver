'''
Verify that a plugin can listen on a port described by TSPortDescriptor.
'''
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

Test.Summary = 'Test the TSPortDescriptor API.'
Test.SkipUnless(Condition.HasProgram('nc', 'nc is required to connect to the plugin port'))


class TestPortDescriptor:
    '''Verify that a plugin can accept connections on a parsed port.'''

    def __init__(self) -> None:
        '''Configure the Traffic Server and client processes.'''
        Test.GetTcpPort('descriptor_port')
        tr = Test.AddTestRun('Connect to the plugin port')
        self._ts = self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        '''Configure Traffic Server with the port descriptor test plugin.

        :return: The Traffic Server process.
        '''
        ts = tr.MakeATSProcess('ts', enable_cache=False)
        plugin_path = os.path.join(Test.Variables.AtsTestPluginsDir, 'port_descriptor.so')
        Test.PrepareTestPlugin(plugin_path, ts, f'{ts.Variables.descriptor_port}:ipv4')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'port_descriptor.*accepted connection', 'Verify the plugin handled the accepted connection.')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'port_descriptor.*unexpected accept event', 'Verify the plugin received the expected accept event.')
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure the client that connects to the plugin port.

        :return: The client process.
        '''
        client = tr.Processes.Default
        client.Command = f'nc -z 127.0.0.1 {self._ts.Variables.descriptor_port}'
        client.ReturnCode = 0
        client.StartBefore(self._ts)
        return client


TestPortDescriptor()
