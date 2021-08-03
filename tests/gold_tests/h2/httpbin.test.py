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

# ----
# Setup Test
# ----
Test.Summary = '''
Test HTTP/2 with httpbin origin server
'''
# Require HTTP/2 enabled Curl
Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
    Condition.HasProgram("shasum", "shasum need to be installed on system for this test to work"),
)
Test.ContinueOnFail = True

# ----
# Setup httpbin Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=False)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(httpbin.Variables.Port)
)
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts.Disk.records_config.update({
    'proxy.config.http.insert_request_via_str': 1,
    'proxy.config.http.insert_response_via_str': 1,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server': 0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http2',

})
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: access
      format: '[%<cqtn>] %<cqtx> %<cqpv> %<cqssv> %<cqssc> %<crc> %<pssc> %<pscl>'

  logs:
    - filename: access
      format: access
'''.split("\n")
)

Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'access.log'), exists=True, content='gold/httpbin_access.gold')

# TODO: when httpbin 0.8.0 or later is released, remove below json pretty print hack
json_printer = '''
python3 -c "import sys,json; print(json.dumps(json.load(sys.stdin), indent=2, separators=(',', ': ')))"
'''

# ----
# Test Cases
# ----

# Test Case 0: Basic request and response
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = "curl -vs -k --http2 https://127.0.0.1:{0}/get | {1}".format(
    ts.Variables.ssl_port, json_printer)
test_run.Processes.Default.ReturnCode = 0
test_run.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
test_run.Processes.Default.StartBefore(Test.Processes.ts)
test_run.Processes.Default.Streams.stdout = "gold/httpbin_0_stdout.gold"
# Different versions of curl will have different cases for HTTP/2 field names.
test_run.Processes.Default.Streams.stderr = Testers.GoldFile("gold/httpbin_0_stderr.gold", case_insensitive=True)
test_run.StillRunningAfter = httpbin

# Test Case 1: Empty response body
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = 'curl -vs -k --http2 https://127.0.0.1:{0}/bytes/0'.format(ts.Variables.ssl_port)
test_run.Processes.Default.ReturnCode = 0
test_run.Processes.Default.Streams.stdout = "gold/httpbin_1_stdout.gold"
# Different versions of curl will have different cases for HTTP/2 field names.
test_run.Processes.Default.Streams.stderr = Testers.GoldFile("gold/httpbin_1_stderr.gold", case_insensitive=True)
test_run.StillRunningAfter = httpbin

# Test Case 2: Chunked
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = 'curl -vs -k --http2 https://127.0.0.1:{0}/stream-bytes/102400?seed=0 | shasum -a 256'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.ReturnCode = 0
test_run.Processes.Default.Streams.stdout = "gold/httpbin_2_stdout.gold"
# Different versions of curl will have different cases for HTTP/2 field names.
test_run.Processes.Default.Streams.stderr = Testers.GoldFile("gold/httpbin_2_stderr.gold", case_insensitive=True)
test_run.StillRunningAfter = httpbin

# Test Case 3: Expect 100-Continue
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = "curl -vs -k --http2 https://127.0.0.1:{0}/post --data 'key=value' -H 'Expect: 100-continue' --expect100-timeout 1 --max-time 5 | {1}".format(
    ts.Variables.ssl_port, json_printer)
test_run.Processes.Default.ReturnCode = 0
test_run.Processes.Default.Streams.stdout = "gold/httpbin_3_stdout.gold"
# Different versions of curl will have different cases for HTTP/2 field names.
test_run.Processes.Default.Streams.stderr = Testers.GoldFile("gold/httpbin_3_stderr.gold", case_insensitive=True)
test_run.StillRunningAfter = httpbin

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'access.log')
)
test_run.Processes.Default.ReturnCode = 0
