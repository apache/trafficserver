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

import sys

Test.Summary = '''
Test ATS TLSv1.3 0-RTT support
'''

# Checking only OpenSSL version allows you to run this test with BoringSSL (and it should pass).
Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'),)

ts1 = Test.MakeATSProcess('ts1', enable_tls=True)
ts2 = Test.MakeATSProcess('ts2', enable_tls=True)
server = Test.MakeOriginServer('server')

request_header1 = {'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
response_header1 = {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n', 'timestamp': '1469733493.993', 'body': 'curl test'}
request_header2 = {'headers': 'GET /early_get HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
response_header2 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted'
}
request_header3 = {
    'headers': 'POST /early_post HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 11\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'knock knock'
}
response_header3 = {
    'headers': 'HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n',
    'timestamp': '1415926535.898',
    'body': ''
}
request_header4 = {
    'headers': 'GET /early_multi_1 HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header4 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted multi_1'
}
request_header5 = {
    'headers': 'GET /early_multi_2 HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header5 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted multi_2'
}
request_header6 = {
    'headers': 'GET /early_multi_3 HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header6 = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'early data accepted multi_3'
}
server.addResponse('sessionlog.json', request_header1, response_header1)
server.addResponse('sessionlog.json', request_header2, response_header2)
server.addResponse('sessionlog.json', request_header3, response_header3)
server.addResponse('sessionlog.json', request_header4, response_header4)
server.addResponse('sessionlog.json', request_header5, response_header5)
server.addResponse('sessionlog.json', request_header6, response_header6)

ts1.addSSLfile('ssl/server.pem')
ts1.addSSLfile('ssl/server.key')

ts1.Setup.Copy('test-0rtt-s_client.py')
ts1.Setup.Copy('h2_early_decode.py')
ts1.Setup.Copy('early_h1_get.txt')
ts1.Setup.Copy('early_h1_post.txt')
ts1.Setup.Copy('early_h2_get.txt')
ts1.Setup.Copy('early_h2_post.txt')
ts1.Setup.Copy('early_h2_multi1.txt')
ts1.Setup.Copy('early_h2_multi2.txt')

ts1.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|ssl_early_data|ssl',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 8,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.max_early_data': 16384,
        'proxy.config.ssl.server.allow_early_data_params': 0,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })

ts1.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts1.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts1.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: example-no.com',
    '  server_max_early_data: 0',
])

ts2.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|ssl_early_data|ssl',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 8,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.max_early_data': 0,
        'proxy.config.ssl.server.allow_early_data_params': 0,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })

ts2.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts2.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts2.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: example-yes.com',
    '  server_max_early_data: 16384',
])

tr = Test.AddTestRun('Basic Curl Test')
tr.MakeCurlCommand('-k --resolve example.com:{0}:127.0.0.1 https://example.com:{0}'.format(ts1.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', 'Making sure the basics still work')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('early data accepted', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 POST)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h1 -t post -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('HTTP/1.1 425 Too Early', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('early data accepted', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/2 GET)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h2 -t get -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/2 POST)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h2 -t post -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(':status 425', 'Only safe methods are allowed')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('early data accepted', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/2 Multiplex)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h2 -t multi1 -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted multi_1', '')
tr.Processes.Default.Streams.All += Testers.ContainsExpression('early data accepted multi_2', '')
tr.Processes.Default.Streams.All += Testers.ContainsExpression('early data accepted multi_3', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/2 Multiplex with POST)')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h2 -t multi2 -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted multi_1', '')
tr.Processes.Default.Streams.All += Testers.ContainsExpression(':status 425', 'Only safe methods are allowed')
tr.Processes.Default.Streams.All += Testers.ContainsExpression('early data accepted multi_3', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET) SNI Provided')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory} -s example.com'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts1

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET) Disabled By SNI Config')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts1.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory} -s example-no.com'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET) Disabled In General')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts2.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts2)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts2

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET) Disabled In General SNI Provided')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts2.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory} -s example.com'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts2

tr = Test.AddTestRun('TLSv1.3 0-RTT Support (HTTP/1.1 GET) Enabled By SNI Config')
tr.Processes.Default.Command = f'{sys.executable} {Test.RunDirectory}/test-0rtt-s_client.py -p {ts2.Variables.ssl_port} -v h1 -t get -r {Test.RunDirectory} -s example-yes.com'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression('early data accepted', '')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('curl test', '')
