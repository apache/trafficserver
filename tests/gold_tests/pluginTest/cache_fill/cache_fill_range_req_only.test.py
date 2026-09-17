'''
Test the cache_fill plugin's --range-req-only option
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
cache_fill --range-req-only=true fills the cache for a range request and
leaves a non-range request alone.
'''

Test.SkipUnless(
    Condition.PluginExists('cache_fill.so'),
    Condition.PluginExists('xdebug.so'),
)
# Skip until cache_fill supports UDS
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())
Test.ContinueOnFail = True
Test.testName = "cache_fill_range_req_only"


class CacheFillRangeReqOnlyTest:
    '''
    --range-req-only=true means "only background fill when the client sent a
    range request".  That decision is made by looking for a Range (or
    conditional) header on the client request, so the test drives both sides of
    it: a range request must fill, a plain request must not.
    '''

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()
        self.curl_and_args = '-s -D /dev/stdout -v -x localhost:{} -H "x-debug: x-cache,x-cache-key"'.format(self.ts.Variables.port)

    def addCacheableResponse(self, path, etag):
        req = {
            "headers": "GET {} HTTP/1.1\r\n".format(path) + "Host: www.example.com\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        res = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "Cache-Control: max-age=300\r\n" + "Connection: close\r\n" + 'Etag: {}\r\n'.format(etag),
            "timestamp": "1469733493.993",
            "body": "hello hello"
        }
        self.server.addResponse("sessionlog.json", req, res)

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")
        # Distinct paths that are not a prefix of one another, so a remap rule
        # cannot match the wrong one.
        self.addCacheableResponse("/fill_on_range", "994324f6-78f6bc3e8d639")
        self.addCacheableResponse("/skip_when_plain", "772102f4-56f4bc1e6d417")

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.remap_config.AddLines(
            [
                'map http://www.example.com/fill_on_range http://127.0.0.1:{}/fill_on_range'.format(self.server.Variables.Port) +
                ' @plugin=cache_fill.so @pparam=--range-req-only=true',
                'map http://www.example.com/skip_when_plain http://127.0.0.1:{}/skip_when_plain'.format(
                    self.server.Variables.Port) + ' @plugin=cache_fill.so @pparam=--range-req-only=true',
            ])

        self.ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,x-cache-key')

        self.ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'cache_fill',
        })

    def test_rangeRequestPrimesCache(self):
        # A range request is what --range-req-only asks for, so the background
        # fetch should run and pull the whole object into cache.  The client
        # still sees the origin's 200 on this first pass.
        tr = Test.AddTestRun("Range request is a miss and starts a background fill")
        ps = tr.Processes.Default
        ps.StartBefore(self.server, ready=When.PortOpen(self.server.Variables.Port))
        ps.StartBefore(Test.Processes.ts)
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/fill_on_range -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "expected the origin's 200")
        tr.StillRunningAfter = self.ts

    def test_rangeRequestFilledCache(self):
        # The fill above must have happened: the object is now served from
        # cache, and ATS synthesizes the 206 from the cached full body.  This is
        # the assertion that fails when the Range header lookup is broken.
        tr = Test.AddTestRun("Second range request is served from the filled cache")
        ps = tr.Processes.Default
        tr.DelayStart = 2  # let the background fetch finish writing to cache
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/fill_on_range -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression(
            "X-Cache: hit-fresh", "expected a cache hit from the background fill")
        ps.Streams.stdout.Content += Testers.ContainsExpression("206 Partial Content", "expected 206 from the cached object")
        ps.Streams.stdout.Content += Testers.ContainsExpression(
            "Content-Range: bytes 0-4/11", "expected Content-Range: bytes 0-4/11")
        tr.StillRunningAfter = self.ts

    def test_plainRequestIsAMiss(self):
        tr = Test.AddTestRun("Non-range request is a miss")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/skip_when_plain', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        tr.StillRunningAfter = self.ts

    def check_plainRequestWasDeclined(self):
        # The other direction of the same decision.  Cache state cannot show
        # this: a plain cacheable response is stored by ordinary proxy caching
        # whether or not the plugin background fills, so both outcomes look the
        # same from the client.  Assert on the plugin's own decision instead.
        # Under a fix that simply always filled, this line would never appear.
        self.ts.Disk.traffic_out.Content = Testers.ContainsExpression(
            "_range_req_only=true; This transaction is not a range request",
            "expected the plain request to be declined as a non-range request")
        # ...and the range request must have been accepted, not declined.
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "scheduling background fetch", "expected the range request to schedule a background fetch")

    def run(self):
        self.test_rangeRequestPrimesCache()
        self.test_rangeRequestFilledCache()
        self.test_plainRequestIsAMiss()
        self.check_plainRequestWasDeclined()


CacheFillRangeReqOnlyTest().run()
