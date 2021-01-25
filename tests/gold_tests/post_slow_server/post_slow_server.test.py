'''
Server receives POST, sent by client over HTTP/2, waits 2 minutes, then sends response with 200 KBytes of data
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
Server receives POST, sent by client over HTTP/2, waits 2 minutes, then sends response with 200 KBytes of data
'''

# Because of the 2 minute delay, we don't want to run this test in CI checks.  Comment out this line to run it.
Test.SkipIf(Condition.true("Test takes too long to run it in CI."))

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)

ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.transaction_no_activity_timeout_out': 150,
    'proxy.config.http2.no_activity_timeout_in': 150,
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

Test.GetTcpPort("server_port")

ts.Disk.remap_config.AddLine(
    'map https://localhost http://localhost:{}'.format(Test.Variables.server_port)
)

server = Test.Processes.Process(
    "server", "bash -c '" + Test.TestDirectory + "/server.sh {}'".format(Test.Variables.server_port)
)

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --request POST --verbose --ipv4 --http2 --insecure --header "Content-Length: 0"' +
    " --header 'Host: localhost' https://localhost:{}/xyz >curl.log 2>curl.err".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)

# Make sure the curl command received 200 KB of data.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = "bash -c '" + Test.TestDirectory + "/check.sh'"
tr.Processes.Default.ReturnCode = 0
