'''
Verify proxy.config.http.bypass_parent bypasses parent selection.
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

Test.Summary = '''
Verify proxy.config.http.bypass_parent bypasses parent.config, next-hop
strategies, plugin-set parents, and CONNECT parenting, and yields to
no_dns_just_forward_to_parent.
'''

Test.ATSReplayTest(replay_file='replays/bypass_parent.replay.yaml')
Test.ATSReplayTest(replay_file='replays/bypass_parent_no_dns.replay.yaml')
Test.ATSReplayTest(replay_file='replays/bypass_parent_plugin_parent.replay.yaml')
Test.ATSReplayTest(replay_file='replays/bypass_parent_cache.replay.yaml')


class BypassParentConnectTest:
    '''CONNECT needs an outbound ip_allow rule for loopback, which ATSReplayTest cannot add.'''

    replay_file = 'replays/bypass_parent_connect.replay.yaml'

    def __init__(self):
        self._server = Test.MakeVerifierServerProcess('server-connect', self.replay_file)
        self._dns = Test.MakeDNServer('dns-connect', default='127.0.0.1')
        self._server.Streams.stdout += Testers.ExcludesExpression(
            'uuid: connect-', 'No CONNECT request reaches the server or the dead parent')
        self._setup_ts()

    def _setup_ts(self):
        self._ts = Test.MakeATSProcess('ts-connect', enable_cache=False)
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http_trans|parent_select',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                # Checked after remap, so this is the remapped origin port rather than the client's :80.
                'proxy.config.http.connect_ports': f'{self._server.Variables.http_port}',
                'proxy.config.http.parent_proxy.self_detect': 0,
                'proxy.config.http.uncacheable_requests_bypass_parent': 0,
                'proxy.config.http.parent_proxy.total_connect_attempts': 1,
                'proxy.config.http.parent_proxy.per_parent_connect_attempts': 1,
            })
        self._ts.Disk.parent_config.AddLine('dest_domain=. parent="127.0.0.1:1|1" go_direct=false parent_is_proxy=true')
        # Remap to a hostname: a 127.0.0.1 target would bypass the parent on its own as a localhost request.
        server_port = self._server.Variables.http_port
        self._ts.Disk.remap_config.AddLines(
            [
                f'map http://connect-direct.test:80/ http://origin.test:{server_port}/ '
                '@plugin=conf_remap.so @pparam=proxy.config.http.bypass_parent=1',
                f'map http://connect-parent.test:80/ http://origin.test:{server_port}/',
            ])
        self._ts.addPrivateConnectAllowYaml()

    def run(self):
        tr = Test.AddTestRun('bypass_parent bypasses the parent for CONNECT per remap rule')
        tr.AddVerifierClientProcess('client-connect', self.replay_file, http_ports=[self._ts.Variables.port])
        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = [self._dns, self._server, self._ts]


BypassParentConnectTest().run()
