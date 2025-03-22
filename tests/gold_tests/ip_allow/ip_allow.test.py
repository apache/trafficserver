'''
Verify ip_allow filtering behavior.
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

Test.Summary = '''
Verify ip_allow filtering behavior.
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
server = Test.MakeOriginServer("server", ssl=True)

testName = ""
request = {
    "headers": "GET /get HTTP/1.1\r\n"
               "Host: www.example.com:80\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response = {
    "headers": "HTTP/1.1 200 OK\r\n"
               "Content-Length: 3\r\n"
               "Connection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
server.addResponse("sessionlog.json", request, response)

# The following shouldn't come to the server, but in the event that there is a
# bug in ip_allow and they are sent through, have them return a 200 OK. This
# will fail the match with the gold file which expects a 403.
request = {
    "headers": "CONNECT www.example.com:80/connect HTTP/1.1\r\n"
               "Host: www.example.com:80\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response = {
    "headers": "HTTP/1.1 200 OK\r\n"
               "Content-Length: 3\r\n"
               "Connection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
server.addResponse("sessionlog.json", request, response)
request = {
    "headers": "PUSH www.example.com:80/h2_push HTTP/2\r\n"
               "Host: www.example.com:80\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response = {
    "headers": "HTTP/2 200 OK\r\n"
               "Content-Length: 3\r\n"
               "Connection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
server.addResponse("sessionlog.json", request, response)

# Configure TLS for Traffic Server for HTTP/2.
ts.addDefaultSSLFiles()

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ip_allow',
        'proxy.config.http.push_method_enabled': 1,
        'proxy.config.http.connect_ports': '{0}'.format(server.Variables.SSL_Port),
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http2.active_timeout_in': 3,
        'proxy.config.http2.max_concurrent_streams_in': 65535,
    })

format_string = (
    '%<cqtd>-%<cqtt> %<stms> %<ttms> %<chi> %<crc>/%<pssc> %<psql> '
    '%<cqhm> %<pquc> %<phr> %<psct> %<{Y-RID}pqh> '
    '%<{Y-YPCS}pqh> %<{Host}cqh> %<{CHAD}pqh>  '
    'sftover=%<{x-safet-overlimit-rules}cqh> sftmat=%<{x-safet-matched-rules}cqh> '
    'sftcls=%<{x-safet-classification}cqh> '
    'sftbadclf=%<{x-safet-bad-classifiers}cqh> yra=%<{Y-RA}cqh> scheme=%<pqus>')

ts.Disk.logging_yaml.AddLines(
    ''' logging:
  formats:
    - name: custom
      format: '{}'
  logs:
    - filename: squid.log
      format: custom
'''.format(format_string).split("\n"))

ts.Disk.remap_config.AddLine('map / https://127.0.0.1:{0}'.format(server.Variables.SSL_Port))

# Note that CONNECT is not in the allowed list.
ts.Disk.ip_allow_yaml.AddLines(
    '''ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods: [GET, HEAD, POST ]
  - apply: in
    ip_addrs: ::/0
    action: allow
    methods: [GET, HEAD, POST ]

'''.split("\n"))

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Line 1 denial for 'CONNECT' from 127.0.0.1", "The CONNECT request should be denied by ip_allow")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Line 1 denial for 'PUSH' from 127.0.0.1", "The PUSH request should be denied by ip_allow")

#
# TEST 1: Perform a GET request. Should be allowed because GET is in the allowlist.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.SSL_Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)

tr.CurlCommand('--verbose -H "Host: www.example.com" http://localhost:{ts_port}/get'.format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/200.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# TEST 2: Perform a CONNECT request. Should not be allowed because CONNECT is
# not in the allowlist.
#
tr = Test.AddTestRun()
tr.CurlCommand('--verbose -X CONNECT -H "Host: localhost" http://localhost:{ts_port}/connect'.format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/403.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# TEST 3: Perform a PUSH request over HTTP/2. Should not be allowed because
# PUSH is not in the allowlist.
#
tr = Test.AddTestRun()
tr.CurlCommand(
    '--http2 --verbose -k -X PUSH -H "Host: localhost" https://localhost:{ts_port}/h2_push'.format(ts_port=ts.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/403_h2.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'stdout_wait') + ' 60 "{} {}" {}'.format(
        os.path.join(Test.TestDirectory, 'run_sed.sh'), os.path.join(ts.Variables.LOGDIR, 'squid.log'),
        os.path.join(Test.TestDirectory, 'gold/log.gold')))
tr.Processes.Default.ReturnCode = 0

IP_ALLOW_CONFIG_ALLOW_ALL = '''ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods: ALL
'''

IP_ALLOW_CONFIG_DENY_ALL = '''ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods: ALL
'''


class Test_ip_allow:
    """Configure a test to verify ip_allow behavior."""

    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(
            self,
            name: str,
            replay_file: str,
            ip_allow_config: str,
            gold_file="",
            replay_keys="",
            is_h3=False,
            expect_request_rejected=False):
        """Initialize the test.

        :param name: The name of the test.
        :param ip_allow_config: The ip_allow configuration to be used.
        :param replay_file: The replay file to be used.
        :param gold_file: (Optional) Gold file to be checked.
        :param replay_keys: (Optional) Keys to be used by pv.
        :param expect_request_rejected: (Optional) Whether or not the client request is expected to be rejected.
        """
        self.name = name
        self.replay_file = replay_file
        self.ip_allow_config = ip_allow_config
        self.gold_file = gold_file
        self.replay_keys = replay_keys
        self.is_h3 = is_h3
        self.expect_request_rejected = expect_request_rejected

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(f"server_{Test_ip_allow.server_counter}", self.replay_file)
        Test_ip_allow.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{Test_ip_allow.ts_counter}", enable_quic=self.is_h3, enable_tls=True)

        Test_ip_allow.ts_counter += 1
        self._ts = ts
        # Configure TLS for Traffic Server.
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'v_quic|quic|http|ip_allow',
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
                'proxy.config.quic.no_activity_timeout_in': 0,
                'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.http.connect_ports': f"{self._server.Variables.http_port}",
            })

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

        # Set ip_allow policy based on the input configuration.
        self._ts.Disk.ip_allow_yaml.AddLines(self.ip_allow_config.split("\n"))

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{Test_ip_allow.client_counter}',
            self.replay_file,
            https_ports=[self._ts.Variables.ssl_port],
            http3_ports=[self._ts.Variables.ssl_port],
            keys=self.replay_keys)
        Test_ip_allow.client_counter += 1

        if self.expect_request_rejected:
            # The client request should time out because ATS rejects it and does
            # not send a response.
            tr.Processes.Default.ReturnCode = 1
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "client.*prohibited by ip-allow policy", "Request should be rejected by ip_allow")
        else:
            # Verify the client request is successful.
            tr.Processes.Default.ReturnCode = 0
            self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                "client.*allowed by ip-allow policy", "Request should be allowed by ip_allow")

        if self.gold_file:
            tr.Processes.Default.Streams.all = self.gold_file


# ip_allow tests for h3.
if Condition.HasATSFeature('TS_USE_QUIC') and Condition.HasCurlFeature('http3'):

    # TEST 4: Perform a request in h3 with ip_allow configured to allow all IPs.
    test0 = Test_ip_allow(
        "h3_allow_all",
        replay_file='replays/h3.replay.yaml',
        ip_allow_config=IP_ALLOW_CONFIG_ALLOW_ALL,
        is_h3=True,
        expect_request_rejected=False)
    test0.run()

    # TEST 5: Perform a request in h3 with ip_allow configured to deny all IPs.
    test1 = Test_ip_allow(
        "h3_deny_all",
        replay_file='replays/h3.replay.yaml',
        ip_allow_config=IP_ALLOW_CONFIG_DENY_ALL,
        is_h3=True,
        expect_request_rejected=True)
    test1.run()

# TEST 6: Verify rules are applied to all methods if methods is not specified.
IP_ALLOW_CONFIG_METHODS_UNSPECIFIED = '''ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
'''
test_ip_allow_optional_methods = Test_ip_allow(
    "ip_allow_optional_methods",
    replay_file='replays/https_multiple_methods.replay.yaml',
    ip_allow_config=IP_ALLOW_CONFIG_METHODS_UNSPECIFIED,
    is_h3=False,
    expect_request_rejected=False)
test_ip_allow_optional_methods.run()
