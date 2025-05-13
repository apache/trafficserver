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
Combined Slice/crr ident test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('header_rewrite.so'),
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%UID}")

# default root
req_header_chk = {
    "headers": "GET / HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "UID: none\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

res_header_chk = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

server.addResponse("sessionlog.json", req_header_chk, res_header_chk)

# Define ATS and configure
ts = Test.MakeATSProcess("ts")

# set up slice plugin with remap host into cache_range_requests
ts.Disk.remap_config.AddLines(
    [
        f'map http://slice/ http://127.0.0.1:{server.Variables.Port}/' +
        ' @plugin=slice.so @pparam=--blockbytes-test=3 @pparam=--remap-host=crr',
        f'map http://crr/ http://127.0.0.1:{server.Variables.Port}/' +
        '  @plugin=cache_range_requests.so @pparam=--consider-ims @pparam=--consider-ident' +
        ' @plugin=header_rewrite.so @pparam=hdr_rw.conf',
    ])

ts.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: 'cpuup=%<cquup> sssc=%<sssc> pssc=%<pssc> phr=%<phr> range=::%<{Range}cqh>:: x-crr-ident=::%<{X-Crr-Ident}cqh>:: uid=::%<{UID}pqh>:: crc=%<crc>'
 logs:
  - filename: transaction
    format: custom
'''.split("\n"))

ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'cache_range_requests|header_rewrite|slice|log',
    })

ts.Disk.MakeConfigFile("hdr_rw.conf").AddLines(
    [
        'cond %{SEND_REQUEST_HDR_HOOK}', 'cond %{HEADER:Range} ="bytes=0-2" [AND]', 'set-header UID %{CLIENT-HEADER:UID} 0'
        '', 'cond %{SEND_REQUEST_HDR_HOOK}', 'cond %{HEADER:Range} ="bytes=3-5" [AND]', 'set-header UID %{CLIENT-HEADER:UID} 1'
    ])

# Test case: short lived asset. second slice should be HIT_FRESH

req_header_plain_0 = {
    "headers": "GET /plain HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "UID: plain 0\r\n" + "Range: bytes=0-2\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

res_header_plain_0 = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" + "Connection: close\r\n" +
        "Content-Range: bytes 0-2/5\r\n" + 'Etag: "plain"\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": "aaa"
}

server.addResponse("sessionlog.json", req_header_plain_0, res_header_plain_0)

req_header_plain_1 = {
    "headers": "GET /plain HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "UID: plain 1\r\n" + "Range: bytes=3-5\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

res_header_plain_1 = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" + "Connection: close\r\n" +
        "Content-Range: bytes 3-4/5\r\n" + 'Etag: "plain"\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": "BB"
}

server.addResponse("sessionlog.json", req_header_plain_1, res_header_plain_1)

# curl helper
curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{}'.format(ts.Variables.port) + ' -H "x-debug: x-cache"'

# 0 Test - Preload plain asset
tr = Test.AddTestRun("Preload plain")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + ' http://slice/plain -H "UID: plain"')
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("aaaBB", "expected aaaBB")
ps.Streams.stdout.Content = Testers.ContainsExpression('Etag: "plain"', "expected etag plain")
tr.StillRunningAfter = ts

# 2 Test - Request again, should result in stale asset
tr = Test.AddTestRun("Request 2nd slice (expect slice1 to be fresh)")
tr.DelayStart = 2  # ensure its really stale
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://slice/plain -H "UID: plain"')
ps.ReturnCode = 0
ps.Streams.stderr = Testers.ContainsExpression("aaaBB", "expected aaaBB")
ps.Streams.stdout.Content = Testers.ContainsExpression('Etag: "plain"', "expected etag plain")
tr.StillRunningAfter = ts

# change out the asset

req_header_chg_0 = {
    "headers": "GET /plain HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "UID: chg 0\r\n" + "Range: bytes=0-2\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

res_header_chg_0 = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=60\r\n" +
        "Connection: close\r\n" + "Content-Range: bytes 0-2/5\r\n" + 'Etag: "chg"\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": "AAA"
}

server.addResponse("sessionlog.json", req_header_chg_0, res_header_chg_0)

req_header_chg_1 = {
    "headers": "GET /plain HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "UID: chg 1\r\n" + "Range: bytes=3-5\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

res_header_chg_1 = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=60\r\n" +
        "Connection: close\r\n" + "Content-Range: bytes 3-4/5\r\n" + 'Etag: "chg"\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": "bb"
}

server.addResponse("sessionlog.json", req_header_chg_1, res_header_chg_1)

# 3 Test - Request again, should result in new asset
tr = Test.AddTestRun("Request again, asset replaced")
tr.DelayStart = 2  # ensure its really stale
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://slice/plain -H "UID: chg"')
ps.ReturnCode = 0
ps.Streams.stderr = Testers.ContainsExpression("AAAbb", "expected AAAbb")
ps.Streams.stdout.Content = Testers.ContainsExpression('Etag: "chg"', "expected etag chg")
tr.StillRunningAfter = ts

# 4 Test - Request again, should all be hit
tr = Test.AddTestRun("Request again, asset replaced but hit")
tr.DelayStart = 2  # ensure its really stale
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://slice/plain -H "UID: chg"')
ps.ReturnCode = 0
ps.Streams.stderr = Testers.ContainsExpression("AAAbb", "expected AAAbb")
ps.Streams.stdout.Content = Testers.ContainsExpression('Etag: "chg"', "expected etag chg")
tr.StillRunningAfter = ts

# 5 Test - add token to transaction log
tr = Test.AddTestRun("Fetch 404.txt asset")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://crr/404.txt')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("404", "expected 404 Not Found response")
tr.StillRunningAfter = ts

condwaitpath = os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')

tslog = os.path.join(ts.Variables.LOGDIR, 'transaction.log')
Test.AddAwaitFileContainsTestRun('Await ts transactions to finish logging.', tslog, '404.txt')

# 6 Check logs
tr = Test.AddTestRun()
tr.Processes.Default.Command = (f"cat {tslog}")
tr.Streams.stdout = "gold/slice_crr_ident.gold"
tr.Processes.Default.ReturnCode = 0
