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

ts1 = Test.MakeATSProcess('ts1', select_ports=True, enable_tls=True)
ts2 = Test.MakeATSProcess('ts2', select_ports=True, enable_tls=True)
server = Test.MakeOriginServer('server')

request_header1 = {
    'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header1 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'curl test'
}
request_header2 = {
    'headers': 'GET /early HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header2 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted'
}
server.addResponse('sessionlog.json', request_header1, response_header1)
server.addResponse('sessionlog.json', request_header2, response_header2)

ts1.addSSLfile('ssl/server.pem')
ts1.addSSLfile('ssl/server.key')

ts1.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.limit': 8,
    'proxy.config.http.server_ports': '{0}:proto=http2;http:ssl'.format(ts1.Variables.ssl_port),
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 512000,
    'proxy.config.ssl.session_cache.timeout': 7200,
    'proxy.config.ssl.session_cache.num_buckets': 32768,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.server.max_early_data': 16384,
    'proxy.config.ssl.server.allow_early_data_params': 0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
})

ts1.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts1.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts2.addSSLfile('ssl/server.pem')
ts2.addSSLfile('ssl/server.key')

ts2.Setup.Copy('early.txt')
ts2.Setup.Copy('test-0rtt-s_client.py')

ts2.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.limit': 8,
    'proxy.config.http.server_ports': '{0}:proto=http2;http:ssl'.format(ts2.Variables.ssl_port),
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 512000,
    'proxy.config.ssl.session_cache.timeout': 7200,
    'proxy.config.ssl.session_cache.num_buckets': 32768,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.server.max_early_data': 16384,
    'proxy.config.ssl.server.allow_early_data_params': 0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
})

ts2.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts2.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

tr1 = Test.AddTestRun('Basic Test')
tr1.Processes.Default.Command = 'curl https://127.0.0.1:{0} -k'.format(ts1.Variables.ssl_port)
tr1.Processes.Default.ReturnCode = 0
tr1.Processes.Default.StartBefore(server)
tr1.Processes.Default.StartBefore(Test.Processes.ts1, ready=When.PortOpen(ts1.Variables.ssl_port))
tr1.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', '')
tr1.Processes.Default.Streams.All += Testers.ExcludesExpression('early data accepted', '')
tr1.StillRunningAfter = server
ts1.Streams.All = Testers.ExcludesExpression('Early Data: 1', 'Must NOT have this new header')

tr2 = Test.AddTestRun('Test ATS TLSv1.3 0-RTT Support')
tr2.Processes.Default.Command = 'python3 test-0rtt-s_client.py {0} {1}'.format(ts2.Variables.ssl_port, Test.RunDirectory)
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.StartBefore(Test.Processes.ts2, ready=When.PortOpen(ts2.Variables.ssl_port))
tr2.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted', '')
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
ts2.Streams.All = Testers.ContainsExpression('Early Data: 1', 'Must have this new header')
