'''
Test PUSHing an object into the cache and the GETting it with a few variations on the client connection protocol.
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
Test PUSHing an object into the cache and the GETting it with a few variations on the client connection protocol.
'''

# NOTE: You can also use this to test client-side communication when GETting very large (multi-GB) objects
# by increasing the value of the obj_kilobytes variable below.  (But do not increase it on any shared branch
# that we do CI runs on.)

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

ts = Test.MakeATSProcess("ts", enable_tls=True)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.records_config.update({
    # Do not accept connections from clients until cache subsystem is operational.
    'proxy.config.http.wait_for_cache': 1,

    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|cache',
    'proxy.config.http.cache.http': 1,  # enable caching.
    'proxy.config.http.cache.required_headers': 0,  # No required headers for caching
    'proxy.config.http.push_method_enabled': 1,
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
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

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'cc ' + Test.TestDirectory + '/push_request.c -o push_request'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'cc ' + Test.TestDirectory + '/check_ramp.c -o check_ramp'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
# Delay on readiness of TS IPv4 ssl port
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
#
# Put object with URL http://localhost/bigobj in cache using PUSH request.
tr.Processes.Default.Command = (
    './push_request {} | nc localhost {}'.format(obj_kilobytes, ts.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

# GET bigobj -- cleartext, HTTP 1.1, IPv4
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --header "Host: localhost"' +
    ' http://localhost:{}/bigobj 2>> log.txt | ./check_ramp {}'
    .format(ts.Variables.port, obj_kilobytes)
)
tr.Processes.Default.ReturnCode = 0

# GET bigobj -- TLS, HTTP 1.1, IPv4
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --insecure --header "Host: localhost"' +
    ' https://localhost:{}/bigobj 2>> log.txt | ./check_ramp {}'
    .format(ts.Variables.ssl_port, obj_kilobytes)
)
tr.Processes.Default.ReturnCode = 0

# GET bigobj -- TLS, HTTP 2, IPv4
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http2 --insecure --header "Host: localhost"' +
    ' https://localhost:{}/bigobj 2>> log.txt | ./check_ramp {}'
    .format(ts.Variables.ssl_port, obj_kilobytes)
)
tr.Processes.Default.ReturnCode = 0

# GET bigobj -- TLS, HTTP 2, IPv6
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv6 --http2 --insecure --header "Host: localhost"' +
    ' https://localhost:{}/bigobj 2>> log.txt | ./check_ramp {}'
    .format(ts.Variables.ssl_portv6, obj_kilobytes)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "sed 's/0</0\\\n</' log.txt | grep -F 200 | grep -F HTTP > log2.txt"
tr.Processes.Default.ReturnCode = 0
