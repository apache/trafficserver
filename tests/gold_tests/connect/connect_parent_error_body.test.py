'''
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

Test.Summary = 'Verify CONNECT error response bodies from parent proxies'
Test.ContinueOnFail = True


class ConnectParentErrorBodyTest:
    replay_file = 'replays/connect_parent_error_body.replay.yaml'

    def __init__(self):
        self._setupParentProxy()
        self._setupTS()

    def _setupParentProxy(self):
        self.parent = Test.MakeVerifierServerProcess('parent-proxy', self.replay_file)
        self.parent.Streams.stdout += Testers.ContainsExpression(
            'CONNECT www.example.com:443 HTTP/1.1', 'Verify that ATS forwards the CONNECT request to the parent proxy.')
        self.parent.Streams.stdout += Testers.ContainsExpression(
            'GET http://www.example.com/next HTTP/1.1', 'Verify that ATS reuses the parent connection for the next request.')

    def _setupTS(self):
        self.ts = Test.MakeATSProcess('ts', enable_cache=False)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|parent',
                'proxy.config.http.connect_ports': '443',
                'proxy.config.http.no_dns_just_forward_to_parent': 1,
                'proxy.config.http.parent_proxy.self_detect': 0,
                'proxy.config.http.server_ports': f'{self.ts.Variables.port}',
                'proxy.config.http.server_session_sharing.pool': 'global',
                'proxy.config.url_remap.remap_required': 0,
            })

        self.ts.Disk.parent_config.AddLine(
            f'dest_domain=. parent="127.0.0.1:{self.parent.Variables.http_port}|1" go_direct=false parent_is_proxy=true')
        self.ts.addPrivateConnectAllowYaml(methods='[ CONNECT, GET ]')

    def run(self):
        tr = Test.AddTestRun('CONNECT error response body from parent proxy')
        tr.AddVerifierClientProcess('client', self.replay_file, http_ports=[self.ts.Variables.port])
        tr.Processes.Default.StartBefore(self.parent)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.parent
        tr.StillRunningAfter = self.ts


ConnectParentErrorBodyTest().run()
