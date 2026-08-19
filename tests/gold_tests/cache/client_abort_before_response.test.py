'''
Verify origin connection handling when a client aborts before the origin responds.
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

Test.Summary = __doc__
Test.ContinueOnFail = True

ORIGIN_SCRIPT = os.path.join(Test.TestDirectory, 'abort_detecting_origin.py')

# The origin waits this long before responding. The client gives up well before
# that, so the origin is still waiting when the client aborts.
ORIGIN_DELAY_SECONDS = 6
CLIENT_TIMEOUT_SECONDS = 2

# How long the test run waits for the origin to reach a conclusion about the
# connection after the client aborts.
ORIGIN_WAIT_SECONDS = ORIGIN_DELAY_SECONDS + 3

# These are printed by abort_detecting_origin.py.
ABORT_DETECTED = 'proxy_closed_connection'
ABORT_NOT_DETECTED = 'proxy_kept_connection_open'


class ClientAbortBeforeResponseTest:
    '''Verify how ATS treats the origin connection when the client goes away.

    A client abort before the origin sends its response header should close the
    origin connection when the operator disables half open connections. TLS and
    HTTP/2 clients cannot half close their connections, so with half open
    connections configured ATS keeps such transactions alive to fill the cache
    with the response the client will not receive. See issue #13549.
    '''

    def __init__(self, name: str, enable_tls: bool, allow_half_open: int, expect_abort: bool):
        '''
        :param name: The name to use for the processes of this test case.
        :param enable_tls: Whether the client talks to ATS over TLS.
        :param allow_half_open: The proxy.config.http.allow_half_open value to configure.
        :param expect_abort: Whether ATS is expected to close the origin connection.
        '''
        self._name = name
        self._enable_tls = enable_tls
        self._allow_half_open = allow_half_open
        self._expect_abort = expect_abort
        port_variable = f'{name}_origin_port'
        Test.GetTcpPort(port_variable)
        self._origin_port = getattr(Test.Variables, port_variable)
        self._setup_ts()

    def _setup_ts(self) -> None:
        self._ts = Test.MakeATSProcess(f'ts_{self._name}', enable_tls=self._enable_tls, enable_cache=True)
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.allow_half_open': self._allow_half_open,
                'proxy.config.http.cache.required_headers': 0,
            })
        if self._enable_tls:
            self._ts.addDefaultSSLFiles()
            self._ts.Disk.records_config.update(
                {
                    'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                    'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                })
            self._ts.Disk.ssl_multicert_yaml.AddLines(
                [
                    'ssl_multicert:',
                    '  - dest_ip: "*"',
                    '    ssl_cert_name: server.pem',
                    '    ssl_key_name: server.key',
                ])
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin_port}/')

    def _client_url(self) -> str:
        if self._enable_tls:
            return f'https://127.0.0.1:{self._ts.Variables.ssl_port}/slow'
        return f'http://127.0.0.1:{self._ts.Variables.port}/slow'

    def run(self) -> None:
        scheme = 'https' if self._enable_tls else 'http'
        tr = Test.AddTestRun(f'Client abort over {scheme} with allow_half_open {self._allow_half_open}')

        origin = tr.Processes.Process(
            f'origin_{self._name}', f'python3 {ORIGIN_SCRIPT} {self._origin_port} --delay {ORIGIN_DELAY_SECONDS}')
        origin.Ready = When.PortOpen(self._origin_port)
        origin.ReturnCode = 0

        if self._expect_abort:
            expected, unexpected = ABORT_DETECTED, ABORT_NOT_DETECTED
        else:
            expected, unexpected = ABORT_NOT_DETECTED, ABORT_DETECTED
        origin.Streams.All += Testers.ContainsExpression(expected, f'The origin should report {expected}.')
        origin.Streams.All += Testers.ExcludesExpression(unexpected, f'The origin should not report {unexpected}.')

        # curl gives up before the origin responds, aborting the request. The
        # sleep afterwards gives the origin time to reach its own conclusion
        # about the connection.
        tr.MakeCurlCommandMulti(
            f'{{curl}} -s -k -o /dev/null --max-time {CLIENT_TIMEOUT_SECONDS} {self._client_url()}; '
            f'sleep {ORIGIN_WAIT_SECONDS}',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(origin)
        tr.StillRunningAfter = self._ts


# The operator disabled half open connections, so ATS should not keep the origin
# connection open for a client that hung up.
ClientAbortBeforeResponseTest('http_half_open_disabled', enable_tls=False, allow_half_open=0, expect_abort=True).run()

if not Condition.CurlUsingUnixDomainSocket():
    ClientAbortBeforeResponseTest('https_half_open_disabled', enable_tls=True, allow_half_open=0, expect_abort=True).run()

    # TLS connections cannot be half closed, but half open connections are
    # configured, so ATS finishes the fetch to fill the cache.
    ClientAbortBeforeResponseTest('https_half_open_enabled', enable_tls=True, allow_half_open=1, expect_abort=False).run()
