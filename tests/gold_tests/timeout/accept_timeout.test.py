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

Test.Summary = 'Testing ATS inactivity timeout'

Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
    Condition.HasProgram("telnet", "Need telnet to shutdown when server shuts down tcp"),
    Condition.HasProgram("nc", "Need nc to send data to server")
)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

ts.addSSLfile("../tls/ssl/server.pem")
ts.addSSLfile("../tls/ssl/server.key")

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.transaction_no_activity_timeout_in': 6,
    'proxy.config.http.accept_no_activity_timeout': 2,
    'proxy.config.net.default_inactivity_timeout': 10,
    'proxy.config.net.defer_accept': 0  # Must turn off defer accept to test the raw TCP case
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# case 1 TLS with no data
tr = Test.AddTestRun("tr")
tr.Setup.Copy("time_client.sh")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'sh ./time_client.sh \'openssl s_client -ign_eof -connect 127.0.0.1:{0}\''.format(
    ts.Variables.ssl_port)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Accept timeout", "Request should fail from the accept timeout")

# case 2 TLS with incomplete request header
tr2 = Test.AddTestRun("tr")
tr2.Processes.Default.Command = 'echo "GET /.html HTTP/1.1" | sh ./time_client.sh \'openssl s_client -ign_eof -connect 127.0.0.1:{0}\''.format(
    ts.Variables.ssl_port)
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "Transaction inactivity timeout", "Request should fail with transaction inactivity timeout")

# case 3 TCP with no data
tr3 = Test.AddTestRun("tr")
tr3.Processes.Default.Command = 'sh ./time_client.sh \'telnet 127.0.0.1 {0}\''.format(ts.Variables.port)
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression("Accept timeout", "Request should fail from the accept timeout")

# case 4 TCP with incomplete request header
tr4 = Test.AddTestRun("tr")
tr4.Setup.Copy("create_request.sh")
tr4.Processes.Default.Command = 'sh  ./time_client.sh \'nc -c ./create_request.sh 127.0.0.1 {0}\''.format(ts.Variables.port)
tr4.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "Transaction inactivity timeout", "Request should fail with transaction inactivity timeout")
