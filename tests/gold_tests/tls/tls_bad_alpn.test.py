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

import os
import ports

Test.Summary = '''
Ensure that handshake fails if invalid alpn string is offered
'''

# Only later versions of openssl support the `-alpn` option.
Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

tr = Test.AddTestRun("alpn banana")
tr.Processes.Default.Command = "openssl s_client -ign_eof -alpn=banana -connect 127.0.0.1:{}".format(ts.Variables.ssl_port)
tr.ReturnCode = 1  # Should fail
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.IncludesExpression("No ALPN negotiated", "Banana should not match a negotiation")

tr = Test.AddTestRun("alpn http/1.1")
tr.Processes.Default.Command = "printf 'GET / HTTP/1.1\r\n\r\n' | openssl s_client -ign_eof -alpn=http/1.1 -connect 127.0.0.1:{}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.IncludesExpression("ALPN protocol: http/1.1", "Successful ALPN")
tr.Processes.Default.Streams.All += Testers.IncludesExpression("HTTP/1.1 400 Host Header Required", "Processed the request")

tr = Test.AddTestRun("no alpn")
tr.Processes.Default.Command = "printf 'GET / HTTP/1.1\r\n\r\n' | openssl s_client -ign_eof -connect 127.0.0.1:{}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.IncludesExpression("No ALPN negotiated", "No ALPN offered, none negotiated")
tr.Processes.Default.Streams.All += Testers.IncludesExpression("HTTP/1.1 400 Host Header Required", "Processed the request")

tr = Test.AddTestRun("alpn h2")
tr.Processes.Default.Command = "curl -k --http2 -v -o /dev/null https://127.0.0.1:{}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.IncludesExpression("ALPN. server accepted.*h2", "negotiated h2")
tr.Processes.Default.Streams.All += Testers.IncludesExpression("HTTP/2 404 ", "Good response")
