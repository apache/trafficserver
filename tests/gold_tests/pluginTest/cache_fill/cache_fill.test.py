'''
Test cache_fill plugin
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
Basic cache_fill plugin test
'''

Test.SkipUnless(
    Condition.PluginExists('cache_fill.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = True
Test.testName = "cache_fill"


class CacheFillTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()
        self.curl_and_args = '-s -D /dev/stdout -v -x localhost:{} -H "x-debug: x-cache,x-cache-key"'.format(self.ts.Variables.port)

    def setUpOriginServer(self):
        # Define and configure origin server
        self.server = Test.MakeOriginServer("server")

        req = {
            "headers": "GET /nostore HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*" + "Range: bytes=0-4\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }

        res = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "Cache-Control: nostore\r\n" + "Connection: close\r\n" + 'Etag: 994324f6-78f6bc3e8d639\r\n',
            "timestamp": "1469733493.993",
            "body": "hello hello"
        }

        self.server.addResponse("sessionlog.json", req, res)

        req = {"headers": "GET /200 HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n", "timestamp": "1469733493.993", "body": ""}

        res = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "Cache-Control: max-age=1\r\n" + "Connection: close\r\n" +
                'Etag: 772102f4-56f4bc1e6d417\r\n',
            "timestamp": "1469733493.993",
            "body": "hello hello"
        }

        self.server.addResponse("sessionlog.json", req, res)

        req = {
            "headers": "GET /range HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*" + "Range: bytes=0-4\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        res = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "Cache-Control: max-age=1\r\n" + "Connection: close\r\n" +
                'Etag: 883213f5-67f5bc2e7d528\r\n',
            "timestamp": "1469733493.993",
            "body": "hello hello"
        }

        self.server.addResponse("sessionlog.json", req, res)

    def setUpTS(self):
        # Define and configure ATS
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.remap_config.AddLines(
            [
                'map http://www.example.com/200 http://127.0.0.1:{}/200 @plugin=cache_fill.so'.format(self.server.Variables.Port),
                'map http://www.example.com/range http://127.0.0.1:{}/range @plugin=cache_fill.so'.format(
                    self.server.Variables.Port),
                'map http://www.example.com/nostore http://127.0.0.1:{}/nostore @plugin=cache_fill.so'.format(
                    self.server.Variables.Port),
                'map http://www.example.com/304 http://127.0.0.1:{}/range @plugin=cache_fill.so'.format(self.server.Variables.Port),
            ])

        self.ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,x-cache-key')

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cache_fill|.*cache.*',
            })

    def test_cacheMiss(self):
        # Cache miss; background fetch should fill cache
        tr = Test.AddTestRun("Cache miss")
        ps = tr.Processes.Default
        ps.StartBefore(self.server, ready=When.PortOpen(self.server.Variables.Port))
        ps.StartBefore(Test.Processes.ts)
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/200', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 OK status")
        tr.StillRunningAfter = self.ts

    def test_cacheHit(self):
        # Cache hit-fresh from background fill
        tr = Test.AddTestRun("Cache hit-fresh")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/200', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 OK status")
        tr.StillRunningAfter = self.ts

    def test_rangeReq_CacheMiss(self):
        # Cache miss; background fetch should fill cache
        tr = Test.AddTestRun("Range cache miss")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/range -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 status")
        tr.StillRunningAfter = self.ts

    def test_rangeReq_CacheHit(self):
        # Cache hit-fresh from background fill
        tr = Test.AddTestRun("Range cache hit-fresh")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/range -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit")
        ps.Streams.stdout.Content += Testers.ContainsExpression("206 Partial Content", "Expected 206 status")
        ps.Streams.stdout.Content += Testers.ContainsExpression(
            "Content-Range: bytes 0-4/11", "Expected Content-Range: bytes 0-4/11")
        tr.StillRunningAfter = self.ts

    def test_noStore_noFill(self):
        # Background fetch should NOT fill cache
        tr = Test.AddTestRun("nostore cache miss")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/nostore -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 status")
        tr.StillRunningAfter = self.ts

    def test_nostore_cacheMiss(self):
        # Cache hit miss because background fill was not triggered
        tr = Test.AddTestRun("nostore cache hit-fresh")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/nostore -r 0-4', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 status")
        tr.StillRunningAfter = self.ts

    def setUpGlobalPlugin(self):
        ## Check global plugin ##
        self.ts.Disk.plugin_config.AddLine('cache_fill.so')

        self.ts.Disk.remap_config.AddLines(
            ['map http://www.example.com/global http://127.0.0.1:{}/global'.format(self.server.Variables.Port)])

        req = {
            "headers": "GET /global HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        res = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "Cache-Control: max-age=1\r\n" + "Connection: close\r\n" +
                'Etag: 661091f3-45f3bc0e5d306\r\n',
            "timestamp": "1469733493.993",
            "body": "hello hello"
        }

        self.server.addResponse("sessionlog.json", req, res)

    def test_global_cacheMiss(self):
        # Global implementation: Cache miss; background fetch should fill cache
        tr = Test.AddTestRun("Cache miss - global implementation")

        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/global', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")

        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 status")
        tr.StillRunningAfter = self.ts

    def test_global_cacheHit(self):
        # Global implementation: Cache hit-fresh from background fill
        tr = Test.AddTestRun("Cache hit-fresh - global implementation")
        ps = tr.Processes.Default
        tr.MakeCurlCommand(self.curl_and_args + ' http://www.example.com/global', ts=self.ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit")
        ps.Streams.stdout.Content += Testers.ContainsExpression("200 OK", "Expected 200 status")
        tr.StillRunningAfter = self.ts

    def runTraffic(self):
        self.test_cacheMiss()
        self.test_cacheHit()
        self.test_rangeReq_CacheMiss()
        self.test_rangeReq_CacheHit()
        self.test_noStore_noFill()
        self.test_nostore_cacheMiss()
        self.setUpGlobalPlugin()
        self.test_global_cacheMiss()
        self.test_global_cacheHit()

    def run(self):
        self.runTraffic()


CacheFillTest().run()
