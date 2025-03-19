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
Slice plugin test for sending ident header
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(Condition.PluginExists('slice.so'),)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# default root
request_header_chk = {
    "headers": "GET / HTTP/1.1\r\n" + "Host: ats\r\n" + "Range: none\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_header_chk = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

server.addResponse("sessionlog.json", request_header_chk, response_header_chk)

block_bytes = 11
body = "lets go surfin now"

etag = '"foo"'
last_modified = "Fri, 07 Mar 2025 18:06:58 GMT"

request_etag = {
    "headers": "GET /etag HTTP/1.1\r\n" + "Host: origin\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_etag = {
    "headers":
        "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + f'Etag: {etag}\r\n' + f'Last-Modified: {last_modified}\r\n' +
        "Cache-Control: max-age=500\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": body,
}

server.addResponse("sessionlog.json", request_etag, response_etag)

request_lm = {
    "headers": "GET /lm HTTP/1.1\r\n" + "Host: origin\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_lm = {
    "headers":
        "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + f'Last-Modified: {last_modified}\r\n' + "Cache-Control: max-age=500\r\n" +
        "\r\n",
    "timestamp": "1469733493.993",
    "body": body,
}

server.addResponse("sessionlog.json", request_lm, response_lm)

# use this second ats instance to serve up the slices

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command='traffic_server_valgrind.sh')
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'slice',
})

ts.Disk.remap_config.AddLines(
    [
        f"map http://preload/ http://127.0.0.1:{server.Variables.Port}",
        f'map http://slice/ http://127.0.0.1:{server.Variables.Port}' +
        f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes}',
        f'map http://slicecustom/ http://127.0.0.1:{server.Variables.Port}' +
        f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes}' + ' @pparam=--crr-ident-header=CrrIdent',
    ])

ts.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: '%<cquup> %<sssc> %<pssc> range=::%<{Range}cqh>:: x-crr-ident=::%<{X-Crr-Ident}cqh>:: crrident=::%<{CrrIdent}cqh>::'
 logs:
  - filename: transaction
    format: custom
'''.split("\n"))

# helpers for curl
curl_and_args = f'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{ts.Variables.port}'

# 0 Test - Preload etag asset
tr = Test.AddTestRun("Preload etag asset")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://preload/etag'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 1 Test - Preload last-modified asset
tr = Test.AddTestRun("Preload last-modified asset")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://preload/lm'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 2 Test - Fetch etag asset
tr = Test.AddTestRun("Fetch etag asset")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/etag'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 3 Test - Fetch etag asset
tr = Test.AddTestRun("Fetch etag asset, custom")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slicecustom/etag'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 4 Test - Fetch last-modified asset
tr = Test.AddTestRun("Fetch last-modified asset, custom")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/lm'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 5 Test - Fetch last-modified asset, custom
tr = Test.AddTestRun("Fetch last-modified asset")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slicecustom/lm'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 6 Test - add token to transaction log
tr = Test.AddTestRun("Fetch last-modified asset")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://prefetch/404.txt'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("404", "expected 404 Not Found response")
tr.StillRunningAfter = ts

condwaitpath = os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')

tslog = os.path.join(ts.Variables.LOGDIR, 'transaction.log')
Test.AddAwaitFileContainsTestRun('Await ts transactions to finish logging.', tslog, '404.txt')

# 6 Check logs
tr = Test.AddTestRun()
tr.Processes.Default.Command = (f"cat {tslog}")
tr.Streams.stdout = "gold/slice_ident.gold"
tr.Processes.Default.ReturnCode = 0
