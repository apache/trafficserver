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

Test.Summary = '''
Test prefetch.so with an optional trailing capture group that does not participate in the match.

The fetch-path-pattern below ends in an optional "(\\?.*)?" group that only participates when the
subject carries a query string.  For a query-less request that group is absent, so pcre_exec() returns
3 -- one past the highest *participating* group -- even though the pattern defines a 3rd group that the
replacement references as $3.  The old code compared $3 against that return value and wrongly rejected
it; a non-participating group must instead substitute an empty string rather than fail the whole
replacement (which previously logged "invalid reference in replacement string: $3" and silently
dropped every prefetch).  Mirrors the production hls/mvod remap pattern
"/(.*-)(\\.m3u8)(\\?.*)?$/$1-0.mp4$3/".
'''

server = Test.MakeOriginServer("server")
for i in list(range(1, 1 + 4)):
    request_header = {
        "headers":
            f"GET /texts/demo-{i} HTTP/1.1\r\n"
            "Host: does.not.matter\r\n"  # But cannot be omitted.
            "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Connection: close\r\n"
                   "Cache-control: max-age=85000\r\n"
                   "\r\n",
        "timestamp": "1469733493.993",
        "body": f"This is the body for demo-{i}.\n"
    }
    server.addResponse("sessionlog.json", request_header, response_header)

dns = Test.MakeDNServer("dns")

ts = Test.MakeATSProcess("ts")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|prefetch',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
    })
ts.Disk.remap_config.AddLine(
    f"map http://domain.in http://127.0.0.1:{server.Variables.Port}" + " @plugin=cachekey.so @pparam=--remove-all-params=true"
    " @plugin=prefetch.so" + " @pparam=--front=true" + " @pparam=--fetch-policy=simple" +
    r" @pparam=--fetch-path-pattern=/(.*-)(\d+)(\?.*)?$/$1{$2+1}$3/" + " @pparam=--fetch-count=3")
ts.ReturnCode = Any(0, -2)

# Belt-and-suspenders next to the gold comparison (which is the primary guard): a regression that
# re-introduces a per-request rejection of the non-participating $3 logs an "invalid reference ..."
# error.  This pattern is valid (3 defined groups, $3 in range) so the compile-time validator never
# fires either, hence no "invalid reference" text should ever reach the log.
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "invalid reference", "optional non-participating group must not fail the replacement")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'echo start TS, HTTP server and DNS.'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.MakeCurlCommand(f'--verbose --proxy 127.0.0.1:{ts.Variables.port} http://domain.in/texts/demo-1')
tr.Processes.Default.ReturnCode = 0

# The original request and the three prefetches are logged independently and may finish out of
# order, so wait for every expected URL to be logged before comparing, and sort both sides so the
# comparison does not depend on completion order.
for tag in ['demo-1', 'demo-2', 'demo-3', 'demo-4']:
    Test.AddAwaitFileContainsTestRun(f'Await {tag} to be logged.', ts.Disk.traffic_out.Name, tag)

tr = Test.AddTestRun()
tr.Processes.Default.Command = (f"grep 'GET http://domain.in' {ts.Disk.traffic_out.Name} | sort")
tr.Streams.stdout = "prefetch_optional_group.gold"
tr.Processes.Default.ReturnCode = 0
