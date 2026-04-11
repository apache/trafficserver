'''
Verify malformed HTTP/2 requests are access logged.
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
import re
import sys

Test.Summary = 'Malformed HTTP/2 requests are logged before transaction creation'


class MalformedH2RequestLoggingTest:
    """
    Exercise malformed and valid HTTP/2 request logging paths.
    """

    REPLAY_FILE = 'replays/h2_malformed_request_logging.replay.yaml'
    MALFORMED_CLIENT = 'malformed_h2_request_client.py'
    MALFORMED_CASES = (
        {
            'scenario': 'connect-missing-authority',
            'uuid': 'malformed-connect',
            'method': 'CONNECT',
            'pqu': '/',
            'description': 'Send malformed HTTP/2 CONNECT request',
        },
        {
            'scenario': 'get-missing-path',
            'uuid': 'malformed-get-missing-path',
            'method': 'GET',
            'pqu': 'https://missing-path.example/',
            'description': 'Send malformed HTTP/2 GET request without :path',
        },
        {
            'scenario': 'get-connection-header',
            'uuid': 'malformed-get-connection',
            'method': 'GET',
            'pqu': 'https://bad-connection.example/bad-connection',
            'description': 'Send malformed HTTP/2 GET request with Connection header',
        },
    )

    def __init__(self):
        self._setup_server()
        self._setup_ts()
        self._processes_started = False
        Test.Setup.CopyAs(self.MALFORMED_CLIENT, Test.RunDirectory)

    @property
    def _squid_log_path(self) -> str:
        return os.path.join(self._ts.Variables.LOGDIR, 'squid.log')

    def _setup_server(self):
        self._server = Test.MakeVerifierServerProcess('malformed-request-server', self.REPLAY_FILE)
        for case in self.MALFORMED_CASES:
            self._server.Streams.stdout += Testers.ExcludesExpression(
                f'uuid: {case["uuid"]}',
                f'{case["description"]} must not reach the origin server.',
            )
        self._server.Streams.stdout += Testers.ContainsExpression(
            'GET /get HTTP/1.1\nuuid: valid-connect',
            reflags=re.MULTILINE,
            description='A valid CONNECT tunnel should still reach the origin.',
        )
        self._server.Streams.stdout += Testers.ContainsExpression(
            r'GET /valid-get HTTP/1\.1\n(?:.*\n)*uuid: valid-get',
            reflags=re.MULTILINE,
            description='A valid non-CONNECT request should still reach the origin.',
        )

    def _setup_ts(self):
        self._ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.File(
            os.path.join(self._ts.Variables.CONFIGDIR, 'storage.config'),
            id='storage_config',
            typename='ats:config',
        )
        self._ts.Disk.storage_config.AddLine('')
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split('\n'))
        self._ts.Disk.File(
            os.path.join(self._ts.Variables.CONFIGDIR, 'ssl_multicert.config'),
            id='ssl_multicert_config',
            typename='ats:config',
        )
        self._ts.Disk.ssl_multicert_config.AddLine('ssl_cert_name=server.pem ssl_key_name=server.key dest_ip=*')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|hpack|http2',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                'proxy.config.http.server_ports': f'{self._ts.Variables.ssl_port}:ssl',
                'proxy.config.http.connect_ports': self._server.Variables.http_port,
            })
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}/')
        self._ts.Disk.logging_yaml.AddLines(
            """
logging:
  formats:
    - name: malformed_h2_request
      format: 'uuid=%<{uuid}cqh> cqpv=%<cqpv> cqhm=%<cqhm> crc=%<crc> sstc=%<sstc> pqu=%<pqu>'
  logs:
    - filename: squid
      format: malformed_h2_request
      mode: ascii
""".split('\n'))
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            'recv headers malformed request',
            'ATS should reject malformed requests at the HTTP/2 layer.',
        )
        for index, case in enumerate(self.MALFORMED_CASES):
            expected = (
                rf'uuid={case["uuid"]} cqpv=http/2 cqhm={case["method"]} '
                rf'crc=ERR_INVALID_REQ sstc=0 pqu={re.escape(case["pqu"])}')
            tester = Testers.ContainsExpression(
                expected,
                f'{case["description"]} should be logged with ERR_INVALID_REQ.',
            )
            if index == 0:
                self._ts.Disk.squid_log.Content = tester
            else:
                self._ts.Disk.squid_log.Content += tester
        self._ts.Disk.squid_log.Content += Testers.ContainsExpression(
            r'uuid=valid-connect cqpv=http/2 cqhm=CONNECT ',
            'A valid HTTP/2 CONNECT should still use the normal transaction log path.',
        )
        self._ts.Disk.squid_log.Content += Testers.ContainsExpression(
            r'uuid=valid-get cqpv=http/2 cqhm=GET ',
            'A valid HTTP/2 GET should still use the normal transaction log path.',
        )
        self._ts.Disk.squid_log.Content += Testers.ExcludesExpression(
            r'uuid=valid-connect .*crc=ERR_INVALID_REQ',
            'Valid HTTP/2 CONNECT logging must not be marked as malformed.',
        )
        self._ts.Disk.squid_log.Content += Testers.ExcludesExpression(
            r'uuid=valid-get .*crc=ERR_INVALID_REQ',
            'Valid HTTP/2 GET logging must not be marked as malformed.',
        )

    def _add_malformed_request_runs(self):
        for case in self.MALFORMED_CASES:
            tr = Test.AddTestRun(case['description'])
            tr.Processes.Default.Command = (
                f'{sys.executable} {self.MALFORMED_CLIENT} {self._ts.Variables.ssl_port} {case["scenario"]}')
            tr.Processes.Default.ReturnCode = 0
            self._keep_support_processes_running(tr)
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                r'Received (RST_STREAM on stream 1 with error code 1|GOAWAY with error code [01])',
                'ATS should reject the malformed request at the HTTP/2 layer.',
            )

    def _add_valid_request_run(self):
        tr = Test.AddTestRun('Send valid HTTP/2 requests')
        tr.AddVerifierClientProcess('valid-request-client', self.REPLAY_FILE, https_ports=[self._ts.Variables.ssl_port])
        self._keep_support_processes_running(tr)

    def _await_malformed_log_entries(self):
        tr = Test.AddAwaitFileContainsTestRun(
            'Await malformed request squid log entries',
            self._squid_log_path,
            'crc=ERR_INVALID_REQ',
            desired_count=len(self.MALFORMED_CASES),
        )
        self._keep_support_processes_running(tr)

    def _keep_support_processes_running(self, tr):
        if self._processes_started:
            tr.StillRunningAfter = self._server
            tr.StillRunningAfter = self._ts
            return

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts
        self._processes_started = True

    def run(self):
        self._add_malformed_request_runs()
        self._add_valid_request_run()
        self._await_malformed_log_entries()


MalformedH2RequestLoggingTest().run()
