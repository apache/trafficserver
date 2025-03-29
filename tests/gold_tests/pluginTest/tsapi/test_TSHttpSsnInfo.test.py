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

Test.Summary = '''
Test TS API to get H2 Session info and H2 metrics
'''

Test.SkipUnless(Condition.HasProgram("nghttp", "Nghttp need to be installed on system for this test to work"),)
Test.ContinueOnFail = True

# ----
# Setup Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# 128ytes
post_body = "0123456789abcdef" * 8
post_body_file = open(os.path.join(Test.RunDirectory, "post_body"), "w")
post_body_file.write(post_body)
post_body_file.close()

# ----
# Setup ATS
# ----
if Condition.HasATSFeature('TS_USE_QUIC') and Condition.HasCurlFeature('http3'):
    ts = Test.MakeATSProcess("ts", enable_tls=True, enable_quic=True)
else:
    ts = Test.MakeATSProcess("ts", enable_tls=True)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()

ts.Disk.remap_config.AddLines(['map /httpbin/ http://127.0.0.1:{0}/'.format(httpbin.Variables.Port)])

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'tsapi', '.libs', 'test_TSHttpSsnInfo.so'), ts)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2|http3|quic|test_TSHttpSsnInfo',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir)
    })

# http2_info.so will output test logging to this file.
log_path = os.path.join(ts.Variables.LOGDIR, "test_TSHttpSsnInfo_plugin_log.txt")
Test.Env["OUTPUT_FILE"] = log_path

# ----
# Test Cases
# ----

# H2 SETTINGS, PRIORITY, HEADERS, CONTINUATION, DATA, GOAWAY
tr = Test.AddTestRun()
tr.TimeOut = 10
tr.Processes.Default.Command = f"nghttp -vn --continuation 'https://localhost:{ts.Variables.ssl_port}/httpbin/post' -d 'post_body'"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "test_TSHttpSsnInfo_nghttp0.gold"
tr.StillRunningAfter = httpbin
tr.StillRunningAfter = ts

# H3
if Condition.HasATSFeature('TS_USE_QUIC') and Condition.HasCurlFeature('http3'):
    tr = Test.AddTestRun()
    tr.TimeOut = 10
    tr.MakeCurlCommand(f"-k --http3 'https://localhost:{ts.Variables.ssl_port}/httpbin/post' -d 'post_body'")
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "test_TSHttpSsnInfo_curl0.gold"
    tr.StillRunningAfter = httpbin
    tr.StillRunningAfter = ts

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo check log"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(log_path)
f.Content = "test_TSHttpSsnInfo_plugin_log.gold"
f.Content += Testers.ContainsExpression(
    "H2 Frames Received:D1,H1,PR5,RS0,S2,PP0,P0,G1,WU0,C1,U0", "Expected numbers of frames should be received")
# We cannot test this on H3 now because the test plugin does not work on H3 sessions
# f.Content += Testers.ContainsExpression("H3 Frames Received:D1,H1,Ra0,CP0,S1,PP0,Rb0,G0,Rc0,Rd0,UND0,UND0,UND0,MPI0,U0",
#                                        "Expected numbers of frames should be received")
