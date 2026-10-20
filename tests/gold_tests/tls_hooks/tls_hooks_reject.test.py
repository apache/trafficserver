'''
Verify that rejecting a connection from a TLS hook closes it.
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
import sys

Test.Summary = '''
A plugin that reenables a TLS hook with TS_EVENT_ERROR must get the connection
closed, whether it reenables from within the hook or later.
'''


class TLSHookRejectTest:
    '''Reject every TLS connection from one hook and verify ATS closes them.'''

    _ts_counter: int = 0

    def __init__(self, hook: str, reject_async: bool) -> None:
        '''Configure ATS with the rejecting plugin and run the probes.

        :param hook: The hook to reject at: vconn_start, client_hello, or cert.
        :param reject_async: Whether the plugin reenables from a scheduled
          continuation rather than from within the hook.
        '''
        self._hook = hook
        self._reject_async = reject_async
        self._description = f'{hook} {"asynchronously" if reject_async else "synchronously"}'
        self._configure_ts()
        self._probe_connections()
        self._verify_connections_released()

    def _configure_ts(self) -> None:
        '''Configure ATS with TLS and the tls_hook_reject plugin.'''
        name = f'ts{TLSHookRejectTest._ts_counter}'
        TLSHookRejectTest._ts_counter += 1
        self._ts = Test.MakeATSProcess(name, enable_tls=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'tls_hook_reject|ssl',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            '''
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
'''.split('\n'))
        plugin_args = f'--hook={self._hook}'
        if self._reject_async:
            plugin_args += ' --async'
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'tls_hook_reject.so'), self._ts, plugin_args)
        if self._reject_async:
            self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(
                'Rejecting connection .* asynchronously', 'The plugin rejected the connection from a scheduled continuation.')
        else:
            self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(
                r'Rejecting connection 0x[0-9a-f]+(?![0-9a-f]| asynchronously)',
                'The plugin rejected the connection from within the hook.')

    def _probe_connections(self) -> None:
        '''Open rejected connections and verify ATS closes each one.'''
        for i in range(2):
            tr = Test.AddTestRun(f'Verify ATS closes a connection rejected at {self._description}: {i}')
            tr.Processes.Default.Command = f'{sys.executable} {Test.TestDirectory}/reject_probe.py {self._ts.Variables.ssl_port}'
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
                'ATS rejected the connection', 'ATS should close a connection a plugin rejected.')
            if i == 0:
                tr.Processes.Default.StartBefore(self._ts)
            tr.StillRunningAfter = self._ts

    def _verify_connections_released(self) -> None:
        '''Verify that ATS released every rejected connection.'''
        tr = Test.AddTestRun(f'Verify ATS released every connection rejected at {self._description}')
        tr.Processes.Default.Command = (
            'for i in $$(seq 1 20); do '
            'open=$$(traffic_ctl metric get proxy.process.net.connections_currently_open | awk "{print \\$$2}"); '
            'if [ "$$open" = "0" ]; then echo "all connections released"; exit 0; fi; '
            'sleep 0.5; '
            'done; '
            'echo "connections still open: $$open"; exit 1')
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            'all connections released', 'Rejected connections should not be left open.')
        tr.StillRunningAfter = self._ts


for hook in ('vconn_start', 'client_hello', 'cert'):
    for reject_async in (False, True):
        TLSHookRejectTest(hook, reject_async)
