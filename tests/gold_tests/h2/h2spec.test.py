'''
Test HTTP/2 with h2spec
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
Test HTTP/2 with httpspec
'''

Test.SkipUnless(
    Condition.HasProgram("h2spec", "h2spec need to be installed on system for this test to work"),
)
Test.ContinueOnFail = True

# ----
# Setup httpbin Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# ----
# Setup ATS. Disable the cache to simplify the test.
# ----
ts = Test.MakeATSProcess("ts", select_ports=False, enable_cache=False)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(httpbin.Variables.Port)
)
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts.Disk.records_config.update({
    'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.http.insert_request_via_str': 1,
    'proxy.config.http.insert_response_via_str': 1,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server': 0,
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http',
})

# ----
# Test Cases
# ----

# Known broken tests are left out (http2/6.4. and http2/6.9.)
h2spec_targets = "http2/1 http2/2 http2/3 http2/4 http2/5 http2/6.1 http2/6.2 http2/6.3 http2/6.5 http2/6.6 http2/6.7 http2/6.8 http2/7 http2/8 hpack"

test_run = Test.AddTestRun()
test_run.Processes.Default.Command = 'h2spec {0} -t -k --timeout 10 -p {1}'.format(h2spec_targets, ts.Variables.ssl_port)
test_run.Processes.Default.ReturnCode = 0
test_run.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
test_run.Processes.Default.StartBefore(Test.Processes.ts)
test_run.Processes.Default.Streams.stdout = "gold/h2spec_stdout.gold"
test_run.StillRunningAfter = httpbin

# Over riding the built in ERROR check since we expect some error cases
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR: HTTP/2", "h2spec tests should have error log")
