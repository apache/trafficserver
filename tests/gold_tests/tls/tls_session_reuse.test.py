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

ts1.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))
ts2.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts1.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts2.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts1.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts1.Variables.SSLDir),
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.session_ticket.number': 2,
    })
ts2.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts2.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts2.Variables.SSLDir),
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.server.session_ticket.enable': 0,
        'proxy.config.ssl.server.session_ticket.number': 0,
    })


def check_session(output_path, tls_ver, reuse_count):
    retval = False
    f = open(output_path, 'r')
    if not f:
        err = "Failed to open {0}".format(output_path)
        return (retval, "Check session is reused", err)

    content = f.read()
    match = re.findall(f'Reused, {tls_ver}', content)
    if len(match) == reuse_count:
        retval = True
        err = "Reused successfully {0} times".format(len(match))
    else:
        err = "Session is not being reused as expected"
    f.close()
    return (retval, "Check session is reused", err)


tr1 = Test.AddTestRun("TLSv1.2 Session Resumption Enabled")
tr1.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_out {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2' \
    .format(ts1.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess1.dat'))
tr1.ReturnCode = 0
tr1.Processes.Default.StartBefore(server)
tr1.Processes.Default.StartBefore(ts1)
tr1.Processes.Default.Streams.All.Content = Testers.Lambda(
    lambda info, tester: check_session(tr1.Processes.Default.Streams.All.AbsPath, 'TLSv1.2', 5))
tr1.StillRunningAfter += server
tr1.StillRunningAfter += ts1

tr2 = Test.AddTestRun("TLSv1.3 Session Resumption Enabled")
tr2.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_out {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2' \
    .format(ts1.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess2.dat'))
tr2.ReturnCode = 0
tr2.Processes.Default.Streams.All.Content = Testers.Lambda(
    lambda info, tester: check_session(tr2.Processes.Default.Streams.All.AbsPath, 'TLSv1.2', 5))
tr2.StillRunningAfter += server

tr3 = Test.AddTestRun("TLSv1.2 Session Resumption Disabled")
tr3.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_out {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_2' \
    .format(ts2.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess3.dat'))
tr3.Processes.Default.StartBefore(ts2)
tr3.Processes.Default.Streams.All = Testers.ExcludesExpression('Reused', '')
tr3.Processes.Default.Streams.All += Testers.ContainsExpression('TLSv1.2', '')
tr3.StillRunningAfter += server
tr3.StillRunningAfter += ts2

tr4 = Test.AddTestRun("TLSv1.3 Session Resumption Disabled")
tr4.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_out {1} -tls1_3 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_3 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_3 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_3 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_3 && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -connect 127.0.0.1:{0} -sess_in  {1} -tls1_3' \
    .format(ts2.Variables.ssl_port, os.path.join(Test.RunDirectory, 'sess4.dat'))
tr4.Processes.Default.Streams.All = Testers.ExcludesExpression('Reused', '')
tr4.Processes.Default.Streams.All += Testers.ContainsExpression('TLSv1.3', '')
