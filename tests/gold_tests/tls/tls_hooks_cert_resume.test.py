'''
Verify that resuming a paused TLS cert hook does not re-run the cert hook stage.
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

Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"))

# The cert hook holds each handshake for two seconds, so this bounds a healthy request well
# while failing a stalled one quickly.
CURL_MAX_TIME = 15


class TestCertHookResume:
    '''A cert hook pauses the handshake and then reenables it.'''

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts_verify = self._configure_verify_client_ts()
        self._ts_switch = self._configure_cert_switch_ts()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server.

        :return: The origin server Process.
        '''
        server = Test.MakeOriginServer("server")
        server.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "ok"
            })
        return server

    def _configure_common(self, ts: 'Process') -> None:
        '''Apply the configuration both Traffic Server processes share.

        :param ts: The Traffic Server Process to configure.
        '''
        ts.addSSLfile("ssl/server.pem")
        ts.addSSLfile("ssl/server.key")
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_hook_test|ssl_client_verify_test',
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.traffic_out.Content = Testers.ContainsExpression(
            r"Cert callback 0 .* - event is good", "the cert hook paused the handshake")

    def _configure_verify_client_ts(self) -> 'Process':
        '''Configure Traffic Server with a pausing cert hook and a verify-client hook.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess("ts_verify", enable_tls=True)
        self._configure_common(ts)
        ts.addSSLfile("ssl/signer.pem")
        ts.Disk.records_config.update({
            'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
        })
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
        ts.Disk.sni_yaml.AddLines([
            'sni:',
            '- fqdn: foo.com',
            '  verify_client: STRICT',
        ])

        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-cert=1')
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_client_verify_test.so'), ts, '-count=1 -good=foo.com')

        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "Client verify callback 0 .* - event is bad", "verify-client hooks must only receive TS_EVENT_SSL_VERIFY_CLIENT")
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Client verify callback 0 .* - event is good good HS", "the verify-client hook checked the client certificate")
        return ts

    def _configure_cert_switch_ts(self) -> 'Process':
        '''Configure Traffic Server with a cert hook that switches certificates while paused.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess("ts_switch", enable_tls=True)
        self._configure_common(ts)
        ts.addSSLfile("ssl/signed-foo.pem")
        ts.addSSLfile("ssl/signed-foo.key")
        ts.addSSLfile("ssl/signed-bar.pem")
        ts.addSSLfile("ssl/signed-bar.key")
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - ssl_cert_name: signed-foo.pem
    ssl_key_name: signed-foo.key
  - ssl_cert_name: signed-bar.pem
    ssl_key_name: signed-bar.key
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        # While the handshake is paused, the cert hook selects the bar.com certificate.
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-cert=1 -cert_switch=bar.com')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Switched ssl_vc=.* to the bar.com certificate", "the cert hook selected the bar.com certificate")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRuns.'''
        port = self._ts_verify.Variables.ssl_port

        tr = Test.AddTestRun("no client certificate requested")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts_verify)
        tr.MakeCurlCommand(
            f"-k -s --max-time {CURL_MAX_TIME} -o /dev/null -w '%{{http_code}}\\n' -H uuid:basic "
            f"--resolve 'random.com:{port}:127.0.0.1' https://random.com:{port}/",
            ts=self._ts_verify)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            "200", "the handshake completed after the cert hook resumed")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts_verify

        tr = Test.AddTestRun("client certificate verified by the verify-client hook")
        tr.Setup.Copy("ssl/signed-foo.pem")
        tr.Setup.Copy("ssl/signed-foo.key")
        tr.MakeCurlCommand(
            f"--tls-max 1.2 -k -s --max-time {CURL_MAX_TIME} -o /dev/null -w '%{{http_code}}\\n' "
            f"--cert ./signed-foo.pem --key ./signed-foo.key -H uuid:basic "
            f"--resolve 'foo.com:{port}:127.0.0.1' https://foo.com:{port}/",
            ts=self._ts_verify)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "the mutual TLS handshake completed")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts_verify

        port = self._ts_switch.Variables.ssl_port
        tr = Test.AddTestRun("the cert hook's certificate survives the resume")
        tr.Processes.Default.StartBefore(self._ts_switch)
        tr.MakeCurlCommand(
            f"-v -k --max-time {CURL_MAX_TIME} -o /dev/null -H uuid:basic "
            f"--resolve 'foo.com:{port}:127.0.0.1' https://foo.com:{port}/",
            ts=self._ts_switch)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression("CN=bar.com", "the client received the hook's certificate")
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            "CN=foo.com", "the configured certificate did not replace it")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts_switch


TestCertHookResume().run()
