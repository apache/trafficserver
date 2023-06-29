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
Test PUSHing an object into the cache and the GETting it with a few variations on the client connection protocol.
'''

# NOTE: You can also use this to test client-side communication when GET-ing very large (multi-GB) objects
# by increasing the value of the obj_kilobytes variable below.  (But do not increase it on any shared branch
# that we do CI runs on.)

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

# push_request and check_ramp are built via `make`. Here we copy the built binary down to the test
# directory so that the test runs in this file can use it.
Test.Setup.Copy(os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'bigobj', 'push_request'))
Test.Setup.Copy(os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'bigobj', 'check_ramp'))

ts = Test.MakeATSProcess("ts1", enable_tls=True)
ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|cache',
    'proxy.config.http.cache.required_headers': 0,  # No required headers for caching
    'proxy.config.http.push_method_enabled': 1,
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.url_remap.remap_required': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://localhost http://localhost'
)

# Set up to check the output after the tests have run.
#
log_id = Test.Disk.File("log2.txt")
log_id.Content = "log2.gold"

# Size of object to get.  (NOTE:  If you increase this significantly you may also have to increase cache
# capacity in tests/gold_tests/autest-size/min_cfg/storage.config.  Also, for very large objects, if
# proxy.config.diags.debug.enabled is 1, the PUSH request will timeout and fail.)
#
obj_kilobytes = 10 * 1024

tr = Test.AddTestRun("PUSH an object to the cache")
# Delay on readiness of TS IPv4 ssl port
tr.Processes.Default.StartBefore(ts)
#
# Put object with URL http://localhost/bigobj in cache using PUSH request.
tr.Processes.Default.Command = (
    f'./push_request {obj_kilobytes} | nc localhost {ts.Variables.port}'
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("GET bigobj: cleartext, HTTP/1.1, IPv4")
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --header "Host: localhost"'
    f' http://localhost:{ts.Variables.port}/bigobj 2>> log.txt |'
    f' ./check_ramp {obj_kilobytes}'
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("GET bigobj: TLS, HTTP/1.1, IPv4")
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --insecure --header "Host: localhost"'
    f' https://localhost:{ts.Variables.ssl_port}/bigobj 2>> log.txt |'
    f' ./check_ramp {obj_kilobytes}'
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("GET bigobj: TLS, HTTP/2, IPv4")
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http2 --insecure --header "Host: localhost"'
    f' https://localhost:{ts.Variables.ssl_port}/bigobj 2>> log.txt |'
    f' ./check_ramp {obj_kilobytes}'
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("GET bigobj: TLS, HTTP/2, IPv6")
tr.Processes.Default.Command = (
    'curl --verbose --ipv6 --http2 --insecure --header "Host: localhost"'
    f' https://localhost:{ts.Variables.ssl_portv6}/bigobj 2>> log.txt |'
    f' ./check_ramp {obj_kilobytes}'
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "sed 's/0</0\\\n</' log.txt | grep -F 200 | grep -F HTTP > log2.txt"
tr.Processes.Default.ReturnCode = 0

# Verify that PUSH requests are rejected when push_method_enabled is 0 (the
# default configuration).
ts = Test.MakeATSProcess("ts2", enable_tls=True)
ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|cache',
    'proxy.config.http.cache.required_headers': 0,  # No required headers for caching
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.url_remap.remap_required': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://localhost http://localhost'
)

tr = Test.AddTestRun("PUSH request is rejected when push_method_enabled is 0")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f'./push_request {obj_kilobytes} | nc localhost {ts.Variables.port}'
)
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "403 Access Denied",
    "The PUSH request should have received a 403 response."
)
