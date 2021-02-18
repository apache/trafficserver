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

import re
Test.Summary = '''
Test tls origin session reuse
'''

# Define default ATS
ts1 = Test.MakeATSProcess("ts1", select_ports=True, enable_tls=True)
ts2 = Test.MakeATSProcess("ts2", select_ports=True, enable_tls=True)
ts3 = Test.MakeATSProcess("ts3", select_ports=True, enable_tls=True)
ts4 = Test.MakeATSProcess("ts4", select_ports=True, enable_tls=True)
ts5 = Test.MakeATSProcess("ts5", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

# Add info the origin server responses
request_header = {
    'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': ''
}
response_header = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'curl test'
}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts1.addSSLfile("ssl/server.pem")
ts1.addSSLfile("ssl/server.key")
ts2.addSSLfile("ssl/server.pem")
ts2.addSSLfile("ssl/server.key")
ts3.addSSLfile("ssl/server.pem")
ts3.addSSLfile("ssl/server.key")
ts4.addSSLfile("ssl/server.pem")
ts4.addSSLfile("ssl/server.key")
ts5.addSSLfile("ssl/server.pem")
ts5.addSSLfile("ssl/server.key")

ts1.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts2.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts3.Disk.remap_config.AddLines([
    'map /ts1 https://127.0.0.1:{0}'.format(ts1.Variables.ssl_port),
    'map /ts2 https://127.0.0.1:{0}'.format(ts2.Variables.ssl_port)
])
ts4.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts5.Disk.remap_config.AddLine(
    'map / https://127.0.0.1:{0}'.format(ts4.Variables.ssl_port)
)

ts1.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts2.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts3.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts4.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts5.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts1.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.origin_session_cache': 1,
    'proxy.config.ssl.origin_session_cache.size': 1
})
ts2.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.origin_session_cache': 1,
    'proxy.config.ssl.origin_session_cache.size': 1
})
ts3.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl.origin_session_cache',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts3.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts3.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 1,
    'proxy.config.ssl.origin_session_cache': 1,
    'proxy.config.ssl.origin_session_cache.size': 1
})
ts4.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts4.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts4.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 0,
    'proxy.config.ssl.origin_session_cache': 1,
    'proxy.config.ssl.origin_session_cache.size': 1
})
ts5.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl.origin_session_cache',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts5.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts5.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 0,
    'proxy.config.ssl.origin_session_cache': 1,
    'proxy.config.ssl.origin_session_cache.size': 1
})

tr = Test.AddTestRun('new session then reuse')
tr.Processes.Default.Command = 'curl https://127.0.0.1:{0}/ts1 -k && curl https://127.0.0.1:{0}/ts1 -k'.format(
    ts3.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.StartBefore(ts3)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', 'Making sure the basics still work')
ts3.Streams.All = Testers.ContainsExpression('new session to origin', '')
ts3.Streams.All += Testers.ContainsExpression('reused session to origin', '')
tr.StillRunningAfter = server
tr.StillRunningAfter += ts3

tr = Test.AddTestRun('remove oldest session, new session then reuse')
tr.Processes.Default.Command = 'curl https://127.0.0.1:{0}/ts2 -k && curl https://127.0.0.1:{0}/ts2 -k'.format(
    ts3.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts2)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', 'Making sure the basics still work')
ts3.Streams.All = Testers.ContainsExpression('remove oldest session', '')
ts3.Streams.All += Testers.ContainsExpression('new session to origin', '')
ts3.Streams.All += Testers.ContainsExpression('reused session to origin', '')
tr.StillRunningAfter = server

tr = Test.AddTestRun('disable tls tickets, reuse should fail')
tr.Processes.Default.Command = 'curl https://127.0.0.1:{0} -k && curl https://127.0.0.1:{0} -k'.format(ts5.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts4)
tr.Processes.Default.StartBefore(ts5)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', 'Making sure the basics still work')
ts5.Streams.All = Testers.ContainsExpression('new session to origin', '')
ts5.Streams.All += Testers.ExcludesExpression('reused session to origin', '')
