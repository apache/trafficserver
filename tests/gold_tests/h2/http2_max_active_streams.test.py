'''
Verify proxy.config.http2.max_active_streams_policy_in enforces the global
active-streams cap and that HPACK stays in sync across refused streams.
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

import sys

Test.Summary = '''
HTTP/2 max_active_streams_policy_in enforcement and HPACK sync test.
'''

CLIENT_SCRIPT = 'h2_max_active_streams.py'


class Http2MaxActiveStreamsTest:
    """Drive concurrent inbound streams past max_active_streams_in."""

    def __init__(self, name: str, replay_file: str, policy: int):
        self._name = name
        self._replay_file = replay_file
        self._policy = policy

    def run(self) -> None:
        tr = Test.AddTestRun(self._name)
        server = tr.AddVerifierServerProcess(f'server-{self._name}', self._replay_file)
        ts = tr.MakeATSProcess(f'ts-{self._name}', enable_tls=True, enable_cache=False)

        ts.addDefaultSSLFiles()
        ts.Setup.CopyAs(f'clients/{CLIENT_SCRIPT}', Test.RunDirectory)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2',
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.http2.max_active_streams_in': 2,
                'proxy.config.http2.max_active_streams_policy_in': self._policy,
                'proxy.config.http2.max_concurrent_streams_in': 100,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.http_port}')
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = (f'{sys.executable} {CLIENT_SCRIPT} {ts.Variables.ssl_port} --streams 4 --probe-from 5')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            'GOAWAY', 'ATS must not tear down the connection; HPACK dynamic table must stay in sync.')

        if self._policy == 1:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                r'stream 5: RST_STREAM error_code=7', 'stream 5 must be refused with REFUSED_STREAM under enforce policy.')
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                r'stream 7: RST_STREAM error_code=7',
                'stream 7 must also be refused with REFUSED_STREAM, proving HPACK decode happened on stream 5.')
            ts.Disk.diags_log.Content = Testers.ContainsExpression(
                r'HTTP/2 stream error code=0x07.*active streams cap reached',
                'ATS should log the cap-reached stream error under enforce policy.')
        else:
            tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
                r'RST_STREAM error_code=7', 'No stream should be refused under advisory policy.')


Http2MaxActiveStreamsTest('enforce', 'replay/http2_max_active_streams_enforce.replay.yaml', policy=1).run()
Http2MaxActiveStreamsTest('advisory', 'replay/http2_max_active_streams_advisory.replay.yaml', policy=0).run()
