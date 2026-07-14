'''
Plugin-initiated redirects must honor proxy.config.http.number_of_redirections.

A plugin that calls TSHttpTxnRedirectUrlSet on every response hook (the test
plugin redirect_rearm does exactly this) must not be able to follow more
redirects than the configured limit. The redirect counter is shared with the
core redirect follower, so a plugin that re-sets the redirect URL on each hop
is still capped at number_of_redirections.
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

Test.Summary = 'A plugin re-setting the redirect URL each hop must not bypass number_of_redirections'

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")

# Chain of 5 hops on the same origin server. Use literal 127.0.0.1:{server.port}
# in the Location headers so ATS's redirect follower doesn't have to do DNS.
ORIGIN = "http://127.0.0.1:{0}".format(server.Variables.Port)

for i in range(1, 5):
    server.addResponse(
        "sessionlog.json", {
            "headers": "GET /r{i} HTTP/1.1\r\nHost: *\r\n\r\n".format(i=i),
            "timestamp": "1",
            "body": ""
        }, {
            "headers": "HTTP/1.1 302 Found\r\nLocation: {0}/r{nxt}\r\nContent-Length: 0\r\n\r\n".format(ORIGIN, nxt=i + 1),
            "timestamp": "1",
            "body": ""
        })

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /r5 HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n",
        "timestamp": "1",
        "body": "final"
    })

# A short chain (/s1 -> /s2 -> 200) that stays within number_of_redirections=2.
# This is the positive case: a legitimate plugin-initiated redirect within the
# limit must still be followed all the way to the terminal 200.
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /s1 HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 302 Found\r\nLocation: {0}/s2\r\nContent-Length: 0\r\n\r\n".format(ORIGIN),
        "timestamp": "1",
        "body": ""
    })
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /s2 HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n",
        "timestamp": "1",
        "body": "shortfinal"
    })

ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'redirect_rearm|http_redirect|http',
        'proxy.config.http.number_of_redirections': 2,
        'proxy.config.http.redirect.actions': 'self:follow,private:follow',
    })

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'redirect_rearm.so'), ts)

ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

tr = Test.AddTestRun()
tr.MakeCurlCommand(
    '-sS -i -x 127.0.0.1:TSPORT http://127.0.0.1:OPORT/r1'.replace('TSPORT', str(ts.Variables.port)).replace(
        'OPORT', str(server.Variables.Port)),
    ts=ts)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.ReturnCode = 0
# With the fix the limit fires and the client sees a 302 returned (the last
# followed hop's response). Without the fix the plugin re-arms the counter on
# every hop and the client sees the final 200 with body "final".
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 302 ", "Client's terminal response must be a 302 from the limit firing, not the final 200")
# Pin the boundary: with number_of_redirections=2 the follower advances r1 -> r2
# -> r3 and returns r3's response, whose Location points at /r4. Asserting the
# terminal Location is /r4 proves exactly two hops were followed, so this case
# cannot pass if a regression instead followed zero hops (Location /r2) or the
# whole chain.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "[Ll]ocation: .*/r4", "Terminal 302 must be the third hop's response, proving exactly two redirects were followed")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "final", "Client must NOT receive the final body (limit bypassed)")

# Positive case: a redirect chain within the limit (one hop) must still be
# followed to completion. Guards against the fix over-correcting and refusing
# legitimate plugin-initiated redirects.
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    '-sS -i -x 127.0.0.1:TSPORT http://127.0.0.1:OPORT/s1'.replace('TSPORT', str(ts.Variables.port)).replace(
        'OPORT', str(server.Variables.Port)),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200 ", "A within-limit plugin-initiated redirect must reach the terminal 200")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "shortfinal", "Client must receive the terminal body for a within-limit redirect")
