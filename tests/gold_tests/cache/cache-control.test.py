'''
Test cached responses and requests with bodies
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
Test cached responses and requests with bodies
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

# **testname is required**
testName = ""
request_header1 = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header1 = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=300\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
request_header2 = {
    "headers": "GET /no_cache_control HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header2 = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "the flinstones"
}
request_header3 = {
    "headers": "GET /max_age_10sec HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header3 = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "yabadabadoo"
}
server.addResponse("sessionlog.json", request_header1, response_header1)
server.addResponse("sessionlog.json", request_header2, response_header2)
server.addResponse("sessionlog.json", request_header3, response_header3)

# ATS Configuration
ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,x-cache-key,via')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.response_via_str': 3,
        'proxy.config.http.insert_age_in_response': 0,
    })

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

# Test 1 - 200 response and cache fill
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "x-debug: x-cache,via" -H "Host: www.example.com" http://localhost:{port}/max_age_10sec'.format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_and_req_body-miss.gold"
tr.StillRunningAfter = ts

# Test 2 - 200 cached response and using netcat
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /max_age_10sec HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_and_req_body-hit.gold"
tr.StillRunningAfter = ts

# Test 3 - response doesn't have cache control directive, so cache-miss every
# time
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /no_cache_control HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_no_cc.gold"
tr.StillRunningAfter = ts

# Test 4 - hit stale cache.
tr = Test.AddTestRun()
tr.Processes.Default.Command = "sleep 15; printf 'GET /max_age_10sec HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_hit_stale.gold"
tr.StillRunningAfter = ts

# Test 5 - only-if-cached. 504 "Not Cached" should be returned if not in cache
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /no_cache_control HTTP/1.1\r\n''Cache-Control: only-if-cached\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''Cache-control: max-age=300\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_no_cache.gold"
tr.StillRunningAfter = ts

#
# Verify correct handling of various max-age directives in both clients and
# responses.
#
ts = Test.MakeATSProcess("ts-for-proxy-verifier")
replay_file = "replay/cache-control-max-age.replay.yaml"
server = Test.MakeVerifierServerProcess("proxy-verifier-server", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,

        # Disable ignoring max-age in the client request so we can test that
        # behavior too.
        'proxy.config.http.cache.ignore_client_cc_max_age': 0,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct max-age cache-control behavior.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("proxy-verifier-client", replay_file, http_ports=[ts.Variables.port])

#
# Verify correct handling of various s-maxage directives in responses.
#
ts = Test.MakeATSProcess("ts-s-maxage")
replay_file = "replay/cache-control-s-maxage.replay.yaml"
server = Test.MakeVerifierServerProcess("s-maxage-server", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct max-age cache-control behavior.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("s-maxage-client", replay_file, http_ports=[ts.Variables.port])

#
# Verify correct interaction between cache-control no-cache and pragma header
#
ts = Test.MakeATSProcess("ts-cache-control-pragma")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|cache',
})
tr = Test.AddTestRun("Verify Pragma: no-cache does not conflict with Cache-Control headers")
replay_file = "replay/cache-control-pragma.replay.yaml"
server = tr.AddVerifierServerProcess("pragma-server", replay_file)
tr.AddVerifierClientProcess("pragma-client", replay_file, http_ports=[ts.Variables.port])
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts


class RequestCacheControlDefaultTest:
    # Verify the proper handling of cache-control directives in requests in
    # default configuration
    requestCacheControlReplayFile = "replay/request-cache-control-default.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            "request-cache-control-default-verifier-server", self.requestCacheControlReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-request-cache-control-default")
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun("Verify the proper handling of cache-control directives in requests in default configuration")
        tr.AddVerifierClientProcess(
            "request-cache-control-default-client",
            self.requestCacheControlReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


RequestCacheControlDefaultTest().run()


class RequestCacheControlHonorClientTest:
    # Verify the proper handling of cache-control directives in requests when
    # ATS is configured to honor client's request to bypass the cache
    requestCacheControlReplayFile = "replay/request-cache-control-honor-client.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            "request-cache-control-honor-client-verifier-server", self.requestCacheControlReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-request-cache-control-honor-client")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Configured to honor client requests to bypass the cache
                "proxy.config.http.cache.ignore_client_no_cache": 0
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

        # Verify logs for the request containing no-cache
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Revalidate document with server", "Verify that ATS honors the no-cache and performs a revalidation.")
        # Verify logs for the request containing no-store
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "client does not permit storing, and cache control does not say to ignore client no-cache",
            "Verify that ATS honors the no-store.")

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the proper handling of cache-control directives in requests when ATS is configured to honor client's request to bypass the cache"
        )
        tr.AddVerifierClientProcess(
            "request-cache-control-honor-client-client",
            self.requestCacheControlReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


RequestCacheControlHonorClientTest().run()


class ResponseCacheControlDefaultTest:
    # Verify the proper handling of cache-control directives in responses in
    # default configuration
    responseCacheControlReplayFile = "replay/response-cache-control-default.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            "response-cache-control-default-verifier-server", self.responseCacheControlReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-response-cache-control-default")
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

        # Verify logs for the response containing no-cache
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Revalidate document with server", "Verify that ATS honors the no-cache in response and performs a revalidation.")
        # Verify logs for the response containing no-store
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "server does not permit storing and config file does not indicate that server directive should be ignored",
            "Verify that ATS honors the no-store in response and bypasses the cache.")

    def runTraffic(self):
        tr = Test.AddTestRun("Verify the proper handling of cache-control directives in responses in default configuration")
        tr.AddVerifierClientProcess(
            "response-cache-control-client-default",
            self.responseCacheControlReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


ResponseCacheControlDefaultTest().run()


class ResponseCacheControlIgnoredTest:
    # Verify the proper handling of cache-control directives in responses when
    # ATS is configured to ignore server's request to bypass the cache
    responseCacheControlReplayFile = "replay/response-cache-control-ignored.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            "response-cache-control-ignored-verifier-server", self.responseCacheControlReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-response-cache-control-ignored")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.cache.ignore_server_no_cache": 1
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

        # Verify logs for the response containing no-cache or no-store
        self.ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "Revalidate document with server",
            "Verify that ATS ignores the no-cache in response and therefore doesn't perform a revalidation.")
        self.ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "server does not permit storing and config file does not indicate that server directive should be ignored",
            "Verify that ATS ignores the no-store in response and caches the responses despite its presence.")

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the proper handling of cache-control directives in responses when ATS is configured to ignore server's request to bypass the cache"
        )
        tr.AddVerifierClientProcess(
            "response-cache-control-client-ignored",
            self.responseCacheControlReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


ResponseCacheControlIgnoredTest().run()
