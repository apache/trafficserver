'''
Test rate_limit plugin: SNI queue expiry does not underflow active counter (Finding #109).

With the bug, when a queued SNI connection expires via max_age, free() is
called on VCONN_CLOSE even though reserve() never succeeded, underflowing
the active counter and crashing on the next reserve() assertion.
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
Test rate_limit SNI queue expiry: active counter underflow regression (Finding #109).
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server", delay=4)
ts = Test.MakeATSProcess("ts", enable_tls=True)

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /slow HTTP/1.1\r\nHost: queue-expiry.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Content-Length: 4\r\n"
                   "Connection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "SLOW"
    })

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /test HTTP/1.1\r\nHost: queue-expiry.example.com\r\n\r\n",
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

# SNI selector with limit=1, queue=5, max_age=1s (1000ms).
# The short max_age causes queued connections to expire quickly.
rate_limit_yaml = os.path.join(ts.Variables.CONFIGDIR, 'rate_limit.yaml')
ts.Disk.File(
    rate_limit_yaml, typename="ats:config").AddLines(
        [
            'selector:',
            '  - sni: queue-expiry.example.com',
            '    limit: 1',
            '    queue:',
            '      size: 5',
            '      max_age: 1',
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

ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')

ts.Disk.plugin_config.AddLine(f'rate_limit.so {rate_limit_yaml}')

RESOLVE = f"--resolve 'queue-expiry.example.com:{ts.Variables.ssl_port}:127.0.0.1'"
BASE_URL = f"https://queue-expiry.example.com:{ts.Variables.ssl_port}"

# Test: Queue expiry regression.
# First request holds the slot (4s origin delay), second gets queued and
# expires after max_age (1s). Health check after proves ATS didn't crash.
tr = Test.AddTestRun("Queue expiry: ATS survives without active counter underflow")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f"curl -sk -o /dev/null '{BASE_URL}/slow' {RESOLVE} & "
    f"sleep 0.5; "
    f"curl -sk -o /dev/null '{BASE_URL}/queued' {RESOLVE} 2>/dev/null; "
    f"wait; sleep 0.5; "
    f"curl -sk -o /dev/null -w '%{{http_code}}' '{BASE_URL}/test' {RESOLVE}")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "200", "Health check after queue expiry should succeed (ATS still alive)")

# Verify ATS didn't crash
ts.Disk.diags_log.Content = Testers.ExcludesExpression("FATAL", "ATS should not crash from active counter underflow")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("ink_release_assert", "No assertion failure from _active underflow")
