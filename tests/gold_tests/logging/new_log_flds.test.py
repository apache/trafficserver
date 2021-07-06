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
Test new log fields
'''

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

# ----
# Setup httpbin Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", enable_tls=True)

ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    # 'proxy.config.diags.debug.enabled': 1,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
})

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:{1}/ip'.format(ts.Variables.port, httpbin.Variables.Port)
)

ts.Disk.remap_config.AddLine(
    'map https://127.0.0.1:{0} http://127.0.0.1:{1}/ip'.format(ts.Variables.ssl_port, httpbin.Variables.Port)
)

ts.Disk.remap_config.AddLine(
    'map https://reallyreallyreallyreallylong.com http://127.0.0.1:{1}/ip'.format(ts.Variables.ssl_port, httpbin.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: "%<ccid> %<ctid> %<cssn>"
  logs:
    - filename: test_new_log_flds
      format: custom
'''.split("\n")
)

tr = Test.AddTestRun()
# Delay on readiness of ssl port
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
#
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}" "http://127.0.0.1:{0}" --http1.1 --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl "https://127.0.0.1:{0}" "https://127.0.0.1:{0}" --http2 --insecure --verbose'.format(
        ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl "https://reallyreallyreallyreallylong.com:{0}" --http2 --insecure --verbose' +
    ' --resolve reallyreallyreallyreallylong.com:{0}:127.0.0.1'
).format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
#
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'test_new_log_flds.log')
)
test_run.Processes.Default.ReturnCode = 0

# Validate generated log.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 {0} < {1}'.format(
    os.path.join(Test.TestDirectory, 'new_log_flds_observer.py'),
    os.path.join(ts.Variables.LOGDIR, 'test_new_log_flds.log'))
tr.Processes.Default.ReturnCode = 0
