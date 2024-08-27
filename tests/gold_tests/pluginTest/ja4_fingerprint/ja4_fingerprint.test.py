#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  'License'); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an 'AS IS' BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import functools
import os
import re
from typing import Any, Callable, Dict, Optional

from ports import get_port

Test.Summary = 'Tests the ja4_fingerprint plugin.'
# The plugin is experimental and therefore may not always be built. It also
# doesn't support QUIC yet.
Test.SkipUnless(Condition.PluginExists('ja4_fingerprint.so'))

TestParams = Dict[str, Any]


class TestJA4Fingerprint:
    '''Configure a test for ja4_fingerprint.'''

    replay_filepath: str = 'ja4_fingerprint.replay.yaml'
    client_counter: int = 0
    server_counter: int = 0
    ts_counter: int = 0

    def __init__(self, name: str, /, autorun: bool) -> None:
        '''Initialize the test.

        :param name: The name of the test.
        '''
        self.name = name
        self.autorun = autorun

    def _init_run(self) -> 'TestRun':
        '''Initialize processes for the test run.'''

        server_one = TestJA4Fingerprint.configure_server('yay.com')
        self._configure_traffic_server(server_one)

        tr = Test.AddTestRun(self.name)
        tr.Processes.Default.StartBefore(server_one)
        tr.Processes.Default.StartBefore(self._ts)

        waiting_tr = self._configure_wait_for_log(server_one)

        return {
            'tr': tr,
            'waiting_tr': waiting_tr,
            'ts': self._ts,
            'server_one': server_one,
            'port_one': self._port_one,
        }

    @classmethod
    def runner(cls, name: str, autorun: bool = True, **kwargs) -> Optional[Callable]:
        '''Create a runner for a test case.

        :param autorun: Run the test case once it's set up. Default is True.
        :return: Returns a runner that can be used as a decorator.
        '''
        test = cls(name, autorun=autorun, **kwargs)._prepare_test_case
        return test

    def _prepare_test_case(self, func: Callable) -> Callable:
        '''Set up a test case and possibly run it.

        :param func: The test case to set up.
        :return: Returns a wrapped function that will have its test params
        passed to it on invocation.
        '''
        functools.wraps(func)
        test_params = self._init_run()

        def wrapper(*args, **kwargs) -> Any:
            return func(test_params, *args, **kwargs)

        if self.autorun:
            wrapper()
        return wrapper

    @staticmethod
    def configure_server(domain: str):
        server = Test.MakeVerifierServerProcess(
            f'server{TestJA4Fingerprint.server_counter + 1}.{domain}',
            TestJA4Fingerprint.replay_filepath,
            other_args='--format \'{url}\'')
        TestJA4Fingerprint.server_counter += 1

        return server

    def _configure_traffic_server(self, server_one: 'Process'):
        '''Configure Traffic Server.

        :param server_one: The origin server process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestJA4Fingerprint.ts_counter + 1}', enable_tls=True)
        TestJA4Fingerprint.ts_counter += 1

        ts.addDefaultSSLFiles()
        self._port_one = get_port(ts, 'PortOne')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.http.server_ports': f'{self._port_one}:ssl',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ja4_fingerprint|http',
            })

        ts.Disk.remap_config.AddLine(f'map / http://localhost:{server_one.Variables.http_port}')

        ts.Disk.ssl_multicert_config.AddLine(f'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        ts.Disk.plugin_config.AddLine(f'ja4_fingerprint.so')

        log_path = os.path.join(ts.Variables.LOGDIR, "ja4_fingerprint.log")
        ts.Disk.File(log_path, id='log_file')

        self._ts = ts

    def _configure_wait_for_log(self, server_one: 'Process'):
        waiting_tr = Test.AddAwaitFileContainsTestRun(self.name, self._ts.Disk.log_file.Name, 'JA4')
        return waiting_tr


# Tests start.


@TestJA4Fingerprint.runner('When we send a request, ' \
                 'then a JA4 header should be attached.')
def test1(params: TestParams) -> None:
    client = params['tr'].Processes.Default
    client.Command = 'curl -k -v "https://localhost:{0}/resource"'.format(params['port_one'])

    client.ReturnCode = 0
    client.Streams.stdout += Testers.ContainsExpression(r'Yay!', 'We should receive the expected body.')
    params['ts'].Disk.traffic_out.Content += Testers.ContainsExpression(
        r'JA4 fingerprint:', 'We should receive the expected log message.')
