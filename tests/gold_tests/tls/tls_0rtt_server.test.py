'''
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
Test ATS TLSv1.3 0-RTT support
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))

ts = Test.MakeATSProcess('ts', select_ports=False)
server = Test.MakeOriginServer('server')

request_header = {
    'headers': 'GET /early HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted'
}
server.addResponse('sessionlog.json', request_header, response_header)

ts.addSSLfile('ssl/server.pem')
ts.addSSLfile('ssl/server.key')

ts.Setup.Copy('early.txt')
ts.Setup.Copy('test-0rtt-s_client.py')

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': '{0}:proto=http2;http:ssl'.format(ts.Variables.ssl_port),
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 512000,
    'proxy.config.ssl.session_cache.timeout': 7200,
    'proxy.config.ssl.session_cache.num_buckets': 32768,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.server.max_early_data': 16384,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

tr = Test.AddTestRun("Test ATS TLSv1.3 0-RTT Support: Create Session")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Command = 'python3 test-0rtt-s_client.py {0} {1}'.format(ts.Variables.ssl_port, Test.RunDirectory)
tr.Processes.Default.Streams.All = 'gold/0rtt_server.gold'
tr.ReturnCode = 0
