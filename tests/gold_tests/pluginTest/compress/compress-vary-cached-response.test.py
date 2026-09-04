'''
Regression test for cached Vary header updates when the client omits
Accept-Encoding.
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
Regression test for compress Vary header updates on cached responses when the
client does not send Accept-Encoding.
'''

Test.SkipUnless(Condition.PluginExists('compress.so'))

server = Test.MakeOriginServer("server")
request_header = {"headers": "GET /object HTTP/1.1\r\nHost: seed.example\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-Control: public, max-age=3600\r\n"
    "Content-Type: text/javascript\r\n"
    "Content-Length: 22\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "var cached_value = 1;\n",
}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'compress',
        'proxy.config.diags.output.diag': 'L',
    }
)
ts.Setup.Copy("etc/compress-cache-false.config")
ts.Disk.remap_config.AddLine(f'map http://seed.example/ http://127.0.0.1:{server.Variables.Port}/')
ts.Disk.remap_config.AddLine(
    f'map http://compress.example/ http://127.0.0.1:{server.Variables.Port}/'
    f' @plugin=compress.so @pparam={Test.RunDirectory}/compress-cache-false.config'
)

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    'handling compression of cached object', 'compress plugin must process a cached response'
)
ts.Disk.diags_log.Content += Testers.ExcludesExpression('cannot add/update the Vary header', 'cached response must not be mutated')
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    'failed to add Vary header for compressible content', 'origin Vary update must succeed'
)
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    'failed to add Vary header to client response', 'client Vary update must succeed'
)

seed = Test.AddTestRun('seed cache without compress plugin')
seed.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
seed.Processes.Default.StartBefore(ts)
seed.Processes.Default.Command = (
    f'curl --http1.1 -sS -o /dev/null --proxy http://127.0.0.1:{ts.Variables.port} http://seed.example/object'
)
seed.Processes.Default.ReturnCode = 0
seed.StillRunningAfter = server
seed.StillRunningAfter = ts

first_cached = Test.AddTestRun('add Vary to cached client response')
first_cached.Processes.Default.Command = (
    f'curl --http1.1 -sS -o /dev/null --proxy http://127.0.0.1:{ts.Variables.port} http://compress.example/object'
)
first_cached.Processes.Default.ReturnCode = 0
first_cached.StillRunningAfter = server
first_cached.StillRunningAfter = ts

headers_path = f'{Test.RunDirectory}/cached_headers.txt'
body_path = f'{Test.RunDirectory}/cached_body.txt'
second_cached = Test.AddTestRun('verify cached client response Vary header')
second_cached.Processes.Default.Command = (
    f'curl --http1.1 -sS -D {headers_path} -o {body_path}'
    f' --proxy http://127.0.0.1:{ts.Variables.port}'
    ' http://compress.example/object'
)
second_cached.Processes.Default.ReturnCode = 0
second_cached.StillRunningAfter = server
second_cached.StillRunningAfter = ts

verify = Test.AddTestRun('verify cached response headers and body')
verify.Processes.Default.Command = (
    f"grep -i '^Vary:.*Accept-Encoding' {headers_path}"
    f" && ! grep -i '^Content-Encoding:' {headers_path}"
    f" && diff {body_path} - <<'EOF'\n"
    'var cached_value = 1;\n'
    'EOF'
)
verify.Processes.Default.ReturnCode = 0
verify.StillRunningAfter = server
verify.StillRunningAfter = ts
