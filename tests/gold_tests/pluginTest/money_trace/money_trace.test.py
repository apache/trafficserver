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
Test money_trace remap
'''

# Test description:

Test.SkipUnless(
    Condition.PluginExists('money_trace.so'),
)
Test.ContinueOnFail = False
Test.testName = "money_trace remap"

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
    f"map http://none/ http://127.0.0.1:{server.Variables.Port}",
    f"map http://basic/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so",
    f"map http://header/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--header=mt",
    f"map http://pregen/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--pregen-header=@pregen",
    f"map http://pgh/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--header=mt @pparam=--pregen-header=@pregen",
    f"map http://create/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--create-if-none=true",
    f"map http://cheader/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--create-if-none=true @pparam=--header=mt",
    f"map http://cpregen/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--create-if-none=true @pparam=--pregen-header=@pregen",
    f"map http://passthru/ http://127.0.0.1:{server.Variables.Port} @plugin=money_trace.so @pparam=--passthru=true",
])

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
      format: 'cqh: %<{X-MoneyTrace}cqh> %<{mt}cqh> %<{@pregen}cqh> pqh: %<{X-MoneyTrace}pqh> %<{mt}pqh> psh: %<{X-MoneyTrace}psh> %<{mt}psh>'
  logs:
    - filename: remap
      format: custom
'''.split("\n")
)

Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'remap.log'),
               exists=True, content='gold/remap-log.gold')

curl_and_args = f"curl -s -D /dev/stdout -o /dev/stderr -x 127.0.0.1:{ts.Variables.port}"

# 0 Test
tr = Test.AddTestRun("no plugin test")
ps = tr.Processes.Default
ps.StartBefore(server)
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + " http://none/path"
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 1 Test
tr = Test.AddTestRun("basic config, no money trace client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + " http://basic/path"
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server


def maketrace(name):
    return f'trace-id={name};parent-id=foo;span-id=bar'


# 2 Test
tr = Test.AddTestRun("basic config, money trace client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://basic/path -H "X-MoneyTrace: ' + maketrace("basic") + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 3 Test
tr = Test.AddTestRun("header config, mt client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://header/path -H "mt: ' + maketrace("header") + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 4 Test
tr = Test.AddTestRun("pregen config, but no header passed in")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://pregen/path'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 5 Test
tr = Test.AddTestRun("pregen config, money trace client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://pregen/path -H "X-MoneyTrace: ' + maketrace("pregen") + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 6 Test
tr = Test.AddTestRun("pregen config, mt client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://pgh/path -H "mt: ' + maketrace("pgh") + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 7 Test
tr = Test.AddTestRun("create config, money trace client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://create/path'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 8 Test
tr = Test.AddTestRun("create config, mt client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://cheader/path'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 9 Test
tr = Test.AddTestRun("create config, pregen client header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://cpregen/path'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 10 Test
tr = Test.AddTestRun("passthru mode")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://passthru/path -H "X-MoneyTrace: ' + maketrace("passthru") + '"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Parsing robustness

trace_strings = [
    "trace-id=spaces; parent-id=foo; span-id=bar",
    "trace-id=noparent;span-id=8215111;span-name=this is some description",
    "trace-id=badspan;span-id=",
    "trace-id=traceonly",
    "trace-id=traceonlysemi;",
    "not a trace header",
]

# 11 Test
for trace in trace_strings:
    tr = Test.AddTestRun(trace)
    ps = tr.Processes.Default
    ps.Command = curl_and_args + ' http://pregen/path -H "X-MoneyTrace: ' + trace + '"'
    ps.ReturnCode = 0
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server

# 11 Test
for trace in trace_strings:
    tr = Test.AddTestRun(trace)
    ps = tr.Processes.Default
    ps.Command = curl_and_args + ' http://cpregen/path -H "X-MoneyTrace: ' + trace + '"'
    ps.ReturnCode = 0
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
# 11 Test
tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'remap.log')
)
