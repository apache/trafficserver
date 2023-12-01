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
Test tls tickets
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
ts2 = Test.MakeATSProcess("ts2", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

# Add info the origin server responses
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts2.addSSLfile("ssl/server.pem")
ts2.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))
ts2.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts2.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.server.session_ticket.enable': '1',
        'proxy.config.ssl.server.ticket_key.filename': '../../file.ticket'
    })
ts2.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts2.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts2.Variables.SSLDir),
        'proxy.config.ssl.server.session_ticket.enable': '1',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.server.ticket_key.filename': '../../file.ticket'
    })

tr = Test.AddTestRun("Create ticket")
tr.Setup.Copy('file.ticket')
tr.Command = 'echo -e "GET / HTTP/1.0\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_out ticket.out'.format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
path1 = tr.Processes.Default.Streams.stdout.AbsPath
tr.StillRunningAfter = server

# Pull out session created in tr to test for session id in tr2


def checkSession(ev):
    retval = False
    f1 = open(path1, 'r')
    f2 = open(path2, 'r')
    err = "Session ids match"
    if not f1 or not f2:
        err = "Failed to open {0} or {1}".format(path1, path2)
        return (retval, "Check that session ids match", err)

    f1Content = f1.read()
    f2Content = f2.read()
    match1 = re.findall('Session-ID: ([0-9A-F]+)', f1Content)
    match2 = re.findall('Session-ID: ([0-9A-F]+)', f2Content)

    if match1 and match2:
        if match1[0] == match2[0]:
            err = "{0} and {1} do match".format(match1[0], match2[0])
            retval = True
        else:
            err = "{0} and {1} do not match".format(match1[0], match2[0])
    else:
        err = "Didn't find session id"
    return (retval, "Check that session ids match", err)


tr2 = Test.AddTestRun("Test ticket")
tr2.Setup.Copy('file.ticket')
tr2.Command = 'echo -e "GET / HTTP/1.0\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in ticket.out'.format(
    ts2.Variables.ssl_port)
tr2.Processes.Default.StartBefore(Test.Processes.ts2)
tr2.ReturnCode = 0
path2 = tr2.Processes.Default.Streams.stdout.AbsPath
tr2.Processes.Default.Streams.All.Content = Testers.Lambda(checkSession)
