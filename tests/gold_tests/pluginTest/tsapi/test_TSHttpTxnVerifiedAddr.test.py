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
Test TS API to get and set a verified address
'''

Test.ContinueOnFail = True

# ----
# Setup Origin Server
# ----
httpbin = Test.MakeHttpBinServer("httpbin")

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts")

ts.Disk.remap_config.AddLines([f'map /httpbin/ http://127.0.0.1:{httpbin.Variables.Port}/'])

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'tsapi', '.libs', 'test_TSHttpTxnVerifiedAddr.so'), ts)

ts.Setup.CopyAs('hrw_verified_addr.conf', Test.RunDirectory)
ts.Disk.plugin_config.AddLine(f'header_rewrite.so --inbound-ip-source=PLUGIN {Test.RunDirectory}/hrw_verified_addr.conf')

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|test_TSHttpTxnVerifiedAddr',
    })

# ----
# Test Cases
# ----

tr = Test.AddTestRun()
tr.MakeCurlCommand(f'-v http://127.0.0.1:{ts.Variables.port}/httpbin/get', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(httpbin, ready=When.PortOpen(httpbin.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression(
    "ip: 1.1.1.1", "Verifiy header_rewrite picked the verified address")
tr.StillRunningAfter = httpbin
tr.StillRunningAfter = ts
