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
# need Curl
Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

# Define default ATS.  "select_ports=False" needed because SSL port used.
#
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

ts.addSSLfile("../remap/ssl/server.pem")
ts.addSSLfile("../remap/ssl/server.key")

ts.Disk.records_config.update({
    # 'proxy.config.diags.debug.enabled': 1,
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
})

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://httpbin.org/ip'.format(ts.Variables.port)
)

ts.Disk.remap_config.AddLine(
    'map https://127.0.0.1:{0} https://httpbin.org/ip'.format(ts.Variables.ssl_port)
)

ts.Disk.remap_config.AddLine(
    'map https://reallyreallyreallyreallylong.com https://httpbin.org/ip'.format(ts.Variables.ssl_port)
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

# Delay to allow TS to flush report to disk, then validate generated log.
#
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = 'python3 {0} < {1}'.format(
    os.path.join(Test.TestDirectory, 'new_log_flds_observer.py'),
    os.path.join(ts.Variables.LOGDIR, 'test_new_log_flds.log'))
tr.Processes.Default.ReturnCode = 0
