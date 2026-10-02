'''
Verify that resuming a paused TLS ClientHello or cert hook continues where it stopped.
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

# Each paused hook holds the handshake for two seconds, so this bounds a healthy request well
# while failing a stalled one quickly.
CURL_MAX_TIME = 15

TLS_VERSIONS = {'TLSv1.2': '--tls-max 1.2', 'TLSv1.3': '--tlsv1.3'}


class TestHookResume:
    '''Paused ClientHello and cert hooks reenable; later hooks must see their own events.'''

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts_verify = self._configure_verify_client_ts()
        self._ts_switch = self._configure_cert_switch_ts()
        self._ts_chain = self._configure_hook_chain_ts()

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

    def _make_ts(self, name: str, hook_args: str) -> 'Process':
        '''Create a Traffic Server with the ssl_hook_test plugin and the shared configuration.

        :param name: The Traffic Server Process name.
        :param hook_args: The ssl_hook_test plugin arguments.
        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(name, enable_tls=True)
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
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, hook_args)
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression("event is bad", "every hook received its own event")
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"Cert callback 0 .* - event is good", "the cert hook paused the handshake")
        return ts

    def _configure_verify_client_ts(self) -> 'Process':
        '''A pausing cert hook alongside a verify-client hook.

        :return: The Traffic Server Process.
        '''
        ts = self._make_ts("ts_verify", '-cert=1')
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
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_client_verify_test.so'), ts, '-count=1 -good=foo.com')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Client verify callback 0 .* - event is good good HS", "the verify-client hook checked the client certificate")
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "(?s)Client verify callback.*Client verify callback", "only the client-authenticated handshake ran the hook")
        return ts

    def _configure_cert_switch_ts(self) -> 'Process':
        '''A pausing ClientHello hook, then a pausing cert hook that switches certificates.

        No servername hook is registered, so the cert hooks are the next ones after ClientHello.

        :return: The Traffic Server Process.
        '''
        ts = self._make_ts("ts_switch", '-client_hello=1 -cert=1 -cert_switch=bar.com')
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
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"Client Hello callback 0 .* - event is good", "the ClientHello hook paused the handshake")
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Switched ssl_vc=.* to the bar.com certificate", "the cert hook selected the bar.com certificate")
        return ts

    def _configure_hook_chain_ts(self) -> 'Process':
        '''A pausing ClientHello hook, a servername hook, and a pausing cert hook.

        :return: The Traffic Server Process.
        '''
        ts = self._make_ts("ts_chain", '-client_hello=1 -sni=1 -cert=1')
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"Client Hello callback 0 .* - event is good", "the ClientHello hook paused the handshake")
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"SNI callback 0 .* - event is good", "the servername hook received TS_EVENT_SSL_SERVERNAME")
        return ts

    def _add_request(self, name: str, ts: 'Process', curl_args: str, sni: str) -> 'TestRun':
        '''Add a TestRun that sends one HTTPS request through ts.

        :param name: The TestRun description.
        :param ts: The Traffic Server Process to send the request to.
        :param curl_args: Extra curl arguments, such as the TLS version.
        :param sni: The server name to request.
        :return: The TestRun.
        '''
        port = ts.Variables.ssl_port
        tr = Test.AddTestRun(name)
        tr.MakeCurlCommand(
            f"-v -k --max-time {CURL_MAX_TIME} -o /dev/null -w 'status=%{{http_code}}\\n' {curl_args} -H uuid:basic "
            f"--resolve '{sni}:{port}:127.0.0.1' https://{sni}:{port}/",
            ts=ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("status=200", "the request completed")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = ts
        return tr

    def run(self) -> None:
        '''Configure and run the TestRuns.'''
        first = True
        for version, version_args in TLS_VERSIONS.items():
            tr = self._add_request(f"{version}: no client certificate requested", self._ts_verify, version_args, "random.com")
            if first:
                tr.Processes.Default.StartBefore(self._server)
                tr.Processes.Default.StartBefore(self._ts_verify)
                first = False

        tr = self._add_request(
            "TLSv1.2: client certificate verified by the verify-client hook", self._ts_verify,
            "--tls-max 1.2 --cert ./signed-foo.pem --key ./signed-foo.key", "foo.com")
        tr.Setup.Copy("ssl/signed-foo.pem")
        tr.Setup.Copy("ssl/signed-foo.key")

        first = True
        for version, version_args in TLS_VERSIONS.items():
            tr = self._add_request(
                f"{version}: the cert hook's certificate survives the resume", self._ts_switch, version_args, "foo.com")
            if first:
                tr.Processes.Default.StartBefore(self._ts_switch)
                first = False
            tr.Processes.Default.Streams.All = Testers.ContainsExpression(
                "CN=bar.com", "the client received the hook's certificate")
            tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
                "CN=foo.com|CN=random.server.com", "no other certificate replaced it")

        first = True
        for version, version_args in TLS_VERSIONS.items():
            tr = self._add_request(f"{version}: each hook stage sees its own event", self._ts_chain, version_args, "foo.com")
            if first:
                tr.Processes.Default.StartBefore(self._ts_chain)
                first = False


TestHookResume().run()
