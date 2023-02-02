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
import os

Test.Summary = '''
Test tls session reuse
'''

# Define default ATS
ts1 = Test.MakeATSProcess("ts1", enable_tls=True)
ts2 = Test.MakeATSProcess("ts2", enable_tls=True)
ts3 = Test.MakeATSProcess("ts3", enable_tls=True)
server = Test.MakeOriginServer("server")


# Add info the origin server responses
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts1.addSSLfile("ssl/server.pem")
ts1.addSSLfile("ssl/server.key")
ts2.addSSLfile("ssl/server.pem")
ts2.addSSLfile("ssl/server.key")
ts3.addSSLfile("ssl/server.pem")
ts3.addSSLfile("ssl/server.key")

ts1.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts2.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts3.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
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

ts1.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache.value': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 0,
})
ts2.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts2.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache.value': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 1,
})
ts3.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts3.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts3.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache.value': 0,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 1,
})


def check_session(ev, test):
    retval = False
    f = open(test.GetContent(ev), 'r')
    err = "Session ids match"
    if not f:
        err = "Failed to open {0}".format(openssl_output)
        return (retval, "Check that session ids match", err)

    content = f.read()
    match = re.findall('Session-ID: ([0-9A-F]+)', content)

    if match:
        if all(i == j for i, j in zip(match, match[1:])):
            err = "{0} reused successfully {1} times".format(match[0], len(match) - 1)
            retval = True
        else:
            err = "Session is not being reused as expected"
    else:
        err = "Didn't find session id"
    return (retval, "Check that session ids match", err)


tr = Test.AddTestRun("TLSv1.2 Session ID")
tr.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_out {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1}' \
    .format(ts1.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess.dat'))
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Streams.All.Content = Testers.Lambda(check_session)
tr.StillRunningAfter = server

tr1 = Test.AddTestRun("TLSv1.2 Session Ticket")
tr1.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_out {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {1}' \
    .format(ts2.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess.dat'))
tr1.ReturnCode = 0
tr1.Processes.Default.StartBefore(ts2)
tr1.Processes.Default.Streams.All.Content = Testers.Lambda(check_session)
tr1.StillRunningAfter = server

tr2 = Test.AddTestRun("Disabled Session Cache")
tr2.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_out {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -no_ticket -sess_in {1}' \
    .format(ts3.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess.dat'))
tr2.ReturnCode = 0
tr2.Processes.Default.StartBefore(ts3)
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression('Reused', '')
