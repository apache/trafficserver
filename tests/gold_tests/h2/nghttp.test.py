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
Test with nghttp
'''

Test.SkipUnless(
    Condition.HasProgram("nghttp", "Nghttp need to be installed on system for this test to work"),
)
Test.ContinueOnFail = True

# ----
# Setup Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# 128KB
post_body = "0123456789abcdef" * 8192
post_body_file = open(os.path.join(Test.RunDirectory, "post_body"), "w")
post_body_file.write(post_body)
post_body_file.close()

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True,
                         enable_tls=True, enable_cache=False)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()

ts.Setup.CopyAs('rules/graceful_shutdown.conf', Test.RunDirectory)

ts.Disk.remap_config.AddLines([
    'map /httpbin/ http://127.0.0.1:{0}/ @plugin=header_rewrite.so @pparam={1}/graceful_shutdown.conf'.format(
        httpbin.Variables.Port, Test.RunDirectory)
])

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http2_cs',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir)
})

# ----
# Test Cases
# ----

# Test Case 0: Trailer
tr = Test.AddTestRun()
tr.TimeOut = 10
tr.Processes.Default.Command = f"nghttp -vn --no-dep 'https://127.0.0.1:{ts.Variables.ssl_port}/httpbin/post' --trailer 'foo: bar' -d 'post_body'"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "gold/nghttp_0_stdout.gold"
tr.StillRunningAfter = httpbin
tr.StillRunningAfter = ts

# Test Case 1: Graceful Shutdown
#   - This test takes 3 seconds to make sure receiving 2 GOAWAY frames
#   - TODO: add a test case of a client keeps the connection open ( -e.g. keep sending PING frame)
tr = Test.AddTestRun()
tr.TimeOut = 10
tr.Processes.Default.Command = f"nghttp -vn --no-dep 'https://127.0.0.1:{ts.Variables.ssl_port}/httpbin/drip?duration=3'"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/nghttp_1_stdout.gold"
tr.StillRunningAfter = httpbin
tr.StillRunningAfter = ts

ts.Disk.traffic_out.Content = "gold/nghttp_ts_stderr.gold"
