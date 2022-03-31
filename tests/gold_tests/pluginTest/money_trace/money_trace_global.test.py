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
Test money_trace global
'''

# Test description:

Test.SkipUnless(
    #    Condition.PluginExists('xdebug.so'),
    Condition.PluginExists('money_trace.so'),
)
Test.ContinueOnFail = False
Test.testName = "money_trace global"

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server", enable_cache=False)

# configure origin server
server = Test.MakeOriginServer("server")

req_chk = {"headers":
           "GET / HTTP/1.1\r\n" + "Host: origin\r\n" + "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }
res_chk = {"headers":
           "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }
server.addResponse("sessionlog.json", req_chk, res_chk)

req_hdr = {"headers":
           "GET /path HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }
res_hdr = {"headers":
           "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }
server.addResponse("sessionlog.json", req_hdr, res_hdr)

ts.Disk.remap_config.AddLines([
    f"map http://ats http://127.0.0.1:{server.Variables.Port}",
])

ts.Disk.plugin_config.AddLine(
    "money_trace.so --pregen-header=@pregen --header=MoneyTrace --create-if-none=true --global-skip-header=Skip-Global-MoneyTrace")

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'money_trace',
})

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: 'cqh: %<{MoneyTrace}cqh> %<{@pregen}cqh> pqh: %<{MoneyTrace}pqh> psh: %<{MoneyTrace}psh>'
  logs:
    - filename: global
      format: custom
'''.split("\n")
)

Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'global.log'),
               exists=True, content='gold/global-log.gold')

curl_and_args = f"curl -s -D /dev/stdout -o /dev/stderr -x 127.0.0.1:{ts.Variables.port}"  # -H 'X-Debug: Probe' "

clientvalue = "trace-id=foo;parent-id=bar;span-id=baz"

# 0 Test
tr = Test.AddTestRun("normal header test")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://ats/path -H "MoneyTrace: ' + clientvalue + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 1 Test
tr = Test.AddTestRun("skip plugin test - pregen will still be set")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path -H "Skip-Global-MoneyTrace: true"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 2 Test
tr = Test.AddTestRun("create header test")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'global.log')
)
#ps.ReturnCode = 0
