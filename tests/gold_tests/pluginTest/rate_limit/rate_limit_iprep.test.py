'''
Test rate_limit plugin: IP reputation initialization (Finding #108).

Validates that ip-rep buckets are properly initialized. The bug used
vector::reserve() instead of resize(), causing UB on indexed writes.
With the fix, ATS starts cleanly and processes TLS connections through
the ip-rep logic without crashing.
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
Test rate_limit ip-rep initialization: reserve() vs resize() regression (Finding #108).
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")
ts = Test.MakeATSProcess("ts", enable_tls=True)

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /test HTTP/1.1\r\nHost: iprep.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Content-Length: 2\r\n"
                   "Connection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "OK"
    })

ts.addDefaultSSLFiles()

# Write the rate_limit YAML config with ip-rep enabled
rate_limit_yaml = os.path.join(ts.Variables.CONFIGDIR, 'rate_limit.yaml')
ts.Disk.File(
    rate_limit_yaml, typename="ats:config").AddLines(
        [
            'ip-rep:',
            '  - name: test-iprep',
            '    buckets: 5',
            '    size: 10',
            '    percentage: 90',
            '    max_age: 300',
            '',
            'selector:',
            '  - sni: iprep.example.com',
            '    limit: 100',
            '    ip-rep: test-iprep',
            '',
        ])

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rate_limit',
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    })

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')

ts.Disk.plugin_config.AddLine(f'rate_limit.so {rate_limit_yaml}')

# Test 1: ATS starts with ip-rep config and handles a TLS request.
# With the reserve() bug, this would crash or produce UB on startup.
tr = Test.AddTestRun("IP reputation init: TLS request through ip-rep selector")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f"curl -sk -o /dev/null -w '%{{http_code}}' "
    f"'https://iprep.example.com:{ts.Variables.ssl_port}/test' "
    f"--resolve 'iprep.example.com:{ts.Variables.ssl_port}:127.0.0.1'")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "200", "TLS request through ip-rep selector should succeed")

# Test 2: Make multiple requests to exercise the ip-rep increment path.
# Each TLS handshake from the same IP increments the reputation counter.
# If buckets were not properly initialized, this triggers the crash.
tr2 = Test.AddTestRun("IP reputation: multiple requests increment counters")
tr2.Processes.Default.Command = (
    f'for i in 1 2 3 4 5; do '
    f'  curl -sk -o /dev/null -w "%{{http_code}} " '
    f'    "https://iprep.example.com:{ts.Variables.ssl_port}/test" '
    f'    --resolve "iprep.example.com:{ts.Variables.ssl_port}:127.0.0.1"; '
    f'done; echo ""')
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout.Content = Testers.ExcludesExpression(
    "000", "No request should get a connection failure (code 000)")

# Verify ATS didn't crash
ts.Disk.diags_log.Content = Testers.ExcludesExpression("FATAL", "ATS should not crash with ip-rep enabled")
