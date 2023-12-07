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

Test.Summary = '''
Test a basic remap of a http connection with Stream Priority Feature
'''

Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
    Condition.HasProgram("shasum", "shasum need to be installed on system for this test to work"),
)
Test.ContinueOnFail = True

# ----
# Setup Origin Server
# ----
server = Test.MakeOriginServer("server")

# Test Case 0:
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /bigfile HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers":
            "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nCache-Control: max-age=3600\r\nContent-Length: 1048576\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    })

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=False)

ts.addDefaultSSLFiles()

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts.Disk.records_config.update(
    {
        'proxy.config.http2.stream_priority_enabled': 1,
        'proxy.config.http2.no_activity_timeout_in': 3,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2',
    })

# ----
# Test Cases
# ----

# Test Case 0:
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -vs -k --http2 https://127.0.0.1:{0}/bigfile | shasum -a 256'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "gold/priority_0_stdout.gold"
# Different versions of curl will have different cases for HTTP/2 field names.
tr.Processes.Default.Streams.stderr = Testers.GoldFile("gold/priority_0_stderr.gold", case_insensitive=True)
tr.StillRunningAfter = server
