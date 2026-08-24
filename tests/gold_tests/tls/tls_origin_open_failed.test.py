'''
Verify that a failed outbound TLS origin connection is surfaced to the client as
an error (5xx) without crashing ATS.
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

import ports

Test.Summary = __doc__


class TestOriginOpenFailed:
    '''Verify a failed outbound TLS connect is surfaced as a 5xx, not a crash.'''

    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._ts = self._configure_trafficserver()

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with an https origin whose connect fails.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestOriginOpenFailed._ts_counter}')
        TestOriginOpenFailed._ts_counter += 1

        # Reserve a port for a TLS origin; its liveness is irrelevant since the
        # source bind below fails before any connect is attempted.
        ports.get_port(ts, 'origin_port')

        ts.Disk.remap_config.AddLine(f'map http://dead.test/ https://127.0.0.1:{ts.Variables.origin_port}/')
        ts.Disk.records_config.update(
            {
                # Bind every outbound connection to a non-local source address
                # (RFC 5737 documentation range) so bind() fails synchronously with
                # EADDRNOTAVAIL, i.e. the connect fails before any TCP/TLS exchange
                # rather than via a later, routable connect that would be refused
                # asynchronously.
                'proxy.config.outgoing_ip_to_bind': '192.0.2.1',
                'proxy.config.http.connect_attempts_max_retries': 1,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|ssl',
            })

        # Tearing down the failed outbound TLS connect must not crash ATS.
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "received signal|failed assertion", "ATS must not crash on a failed outbound TLS connect")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRun.'''
        tr = Test.AddTestRun("a failed outbound TLS connect is surfaced cleanly")
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}" -H "Host: dead.test" http://127.0.0.1:{self._ts.Variables.port}/', ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        # A failed origin connection is surfaced as a 5xx (502 Bad Gateway).
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("50[02]", "a failed origin connect yields a 5xx")
        tr.StillRunningAfter = self._ts


TestOriginOpenFailed().run()
