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
microserver = Test.MakeOriginServer("microserver")

# 128KB
post_body = "0123456789abcdef" * 8192
post_body_file = open(os.path.join(Test.RunDirectory, "post_body"), "w")
post_body_file.write(post_body)
post_body_file.close()

# For Test Case 0
microserver.addResponse("sessionlog.json",
                        {"headers": "POST /post HTTP/1.1\r\nHost: www.example.com\r\nTrailer: foo\r\n\r\n",
                         "timestamp": "1469733493.993",
                         "body": post_body},
                        {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
                            "timestamp": "1469733493.993",
                            "body": ""})

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map /post http://127.0.0.1:{0}/post'.format(microserver.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.cache.http': 0,
    'proxy.config.http2.active_timeout_in': 3,
})

# ----
# Test Cases
# ----

# Test Case 0:  Trailer
tr = Test.AddTestRun()
tr.Processes.Default.Command = "nghttp -v --no-dep 'https://127.0.0.1:{0}/post' --trailer 'foo: bar' -d 'post_body'".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(microserver)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "gold/nghttp_0_stdout.gold"
tr.StillRunningAfter = microserver
