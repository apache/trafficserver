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

Test.Summary = '''
Verify ip_allow filtering behavior.
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True, enable_tls=True, enable_cache=False)
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
        'proxy.config.diags.debug.tags': 'ip-allow',
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
    '%<cqhm> %<cquc> %<phr>/%<pqsn> %<psct> %<{Y-RID}pqh> '
    '%<{Y-YPCS}pqh> %<{Host}cqh> %<{CHAD}pqh>  '
    'sftover=%<{x-safet-overlimit-rules}cqh> sftmat=%<{x-safet-matched-rules}cqh> '
    'sftcls=%<{x-safet-classification}cqh> '
    'sftbadclf=%<{x-safet-bad-classifiers}cqh> yra=%<{Y-RA}cqh> scheme=%<cqus>')

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

tr.Processes.Default.Command = (
    'curl --verbose -H "Host: www.example.com" http://localhost:{ts_port}/get'.format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/200.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# TEST 2: Perform a CONNECT request. Should not be allowed because CONNECT is
# not in the allowlist.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose -X CONNECT -H "Host: localhost" http://localhost:{ts_port}/connect'.format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/403.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# TEST 3: Perform a PUSH request over HTTP/2. Should not be allowed because
# PUSH is not in the allowlist.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --http2 --verbose -k -X PUSH -H "Host: localhost" https://localhost:{ts_port}/h2_push'.format(
        ts_port=ts.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/403_h2.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
