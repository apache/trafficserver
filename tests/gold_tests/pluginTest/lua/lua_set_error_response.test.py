'''Verify ts.client_response.set_error_resp functionality.'''
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

Test.Summary = '''
Test lua functionality
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)


class TestLuaSetErrorResponse:
    '''Verify ts.client_response.set_error_resp functionality.'''

    replay_file: str = 'set_error_response.replay.yaml'
    lua_script: str = 'set_error_response.lua'

    def __init__(self):
        tr = Test.AddTestRun("Lua ts.client_response.set_error_resp")
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_ts(tr)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        '''Configure the DNS server.

        :param tr: The test run to configure the DNS server in.
        :return: The newly created DNS process.
        '''
        dns = tr.MakeDNServer("dns", default='127.0.0.1')
        self._dns = dns
        return dns

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        '''Configure the origin server.

        :param tr: The test run to configure the server in.
        :return: The newly created server process.
        '''
        server = tr.AddVerifierServerProcess("server", self.replay_file)
        self._server = server
        return server

    def _configure_ts(self, tr: 'TestRun') -> 'Process':
        '''Configure the ATS process.

        :param tr: The test run to configure the ATS process in.
        :return: The newly created ATS process.
        '''
        ts = tr.MakeATSProcess("ts", enable_cache=False)
        self._ts = ts
        ts.Setup.Copy(self.lua_script, ts.Variables.CONFIGDIR)
        port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map / http://backend.example.com:{port}/ @plugin=tslua.so @pparam={self.lua_script}')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ts_lua|http',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL'
            })
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure the client process.

        :param tr: The test run to configure the client process in.
        :return: The newly created client process.
        '''
        port = self._ts.Variables.port
        client = tr.AddVerifierClientProcess('client', self.replay_file, http_ports=[port])
        self._client = client
        client.StartBefore(self._server)
        client.StartBefore(self._dns)
        client.StartBefore(self._ts)
        client.Streams.All += Testers.ContainsExpression(
            "HTTP/1.1 418 I'm a teapot", 'The modified HTTP 418 response should be received by the client.')
        client.Streams.All += Testers.ContainsExpression('bad luck', 'The modified HTTP response body should contain "bad luck".')
        return client


TestLuaSetErrorResponse()
