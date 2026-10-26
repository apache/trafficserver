'''
Verify that a client gets a 502 response when the origin truncates a body
that compress.so is transforming.

When the origin sends response headers but none of the body, then either
closes the connection or stalls until the server no-activity timeout, the
tunnel aborts the transform before it ever ran and signaled
TRANSFORM_READ_READY. The HttpSM must not keep waiting for the transform in
that case: the client never got response headers and, over HTTP/2, the stream
would otherwise hang until the client gave up.
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

from ports import get_port

Test.Summary = '''
A truncated origin body during a compress.so transform yields a 502 response
'''

Test.SkipUnless(Condition.PluginExists('compress.so'), Condition.HasCurlFeature('http2'))


class CompressTruncatedOriginTest:

    # Long enough for the stall case (server no-activity timeout of 2 seconds)
    # to complete, but shorter than the client no-activity timeout so that an
    # HttpSM stuck waiting on the transform makes curl time out.
    _client_max_time = 10

    def __init__(self):
        self._first_run = True
        self._setupOrigin()
        self._setupTS()
        self._addRun('HTTP/2, origin closes mid-body', '--http2', 'close')
        self._addRun('HTTP/2, origin stalls mid-body', '--http2', 'stall')
        self._addRun('HTTP/1.1, origin closes mid-body', '--http1.1', 'close')

    def _setupOrigin(self):
        self._origin = Test.Processes.Process('origin')
        port = get_port(self._origin, 'http_port')
        self._origin.Command = f'{sys.executable} {Test.TestDirectory}/compress_truncated_origin.py --port {port}'
        self._origin.Ready = When.PortOpenv4(port)

    def _setupTS(self):
        self._ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|compress|http_tunnel|http2_stream',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                'proxy.config.http.transaction_no_activity_timeout_out': 2,
                'proxy.config.http.transaction_no_activity_timeout_in': 30,
                'proxy.config.http.connect_attempts_max_retries': 0,
            })
        self._ts.Disk.remap_config.AddLine(
            f'map / http://127.0.0.1:{self._origin.Variables.http_port}/'
            f' @plugin=compress.so @pparam={Test.TestDirectory}/compress.config')
        self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            'HTTP/2 unknown case', 'The HTTP/2 stream must not be left waiting on its inactivity timeout')

    def _addRun(self, description, protocol, mode):
        tr = Test.AddTestRun(description)
        client = tr.Processes.Default
        client.Command = (
            f'curl {protocol} -sk -o /dev/null -w "%{{http_code}}\\n"'
            f' --max-time {self._client_max_time}'
            f' -H "Accept-Encoding: gzip"'
            f' https://127.0.0.1:{self._ts.Variables.ssl_port}/{mode}/{protocol.lstrip("-")}/script.js')
        client.ReturnCode = 0
        client.Streams.stdout = Testers.ContainsExpression(r'^502$', 'The client should receive a 502 response')
        if self._first_run:
            client.StartBefore(self._origin)
            client.StartBefore(self._ts)
            self._first_run = False
        tr.StillRunningAfter = self._ts


CompressTruncatedOriginTest()
