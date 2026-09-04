'''
Test background_fetch plugin rule matching.
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
Verify background_fetch Content-Length and wildcard exclusion rules.
'''

Test.SkipUnless(Condition.PluginExists('background_fetch.so'))
Test.ContinueOnFail = True


class BackgroundFetchRuleTest:
    """
    Verify background_fetch rule matching for range requests.
    """

    def __init__(self):
        self._ts = Test.MakeATSProcess("ts")
        self._server = Test.MakeOriginServer("server", lookup_key="{PATH}{%Range}")
        self._configure_server()
        self._configure_ats()

    def _add_range_response(self, path, host):
        range_request = {
            "headers": f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nAccept: */*\r\nRange: bytes=0-4\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "",
        }
        range_response = {
            "headers":
                "HTTP/1.1 206 Partial Content\r\n"
                "Connection: close\r\n"
                "Cache-Control: max-age=600\r\n"
                "Content-Range: bytes 0-4/10\r\n"
                "Content-Length: 5\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "hello",
        }

        self._server.addResponse("sessionlog.json", range_request, range_response)

    def _configure_server(self):
        for path, host in (
            ("allowed", "allowed.example"),
            ("small", "small.example"),
            ("wildcard", "wildcard.example"),
            ("above-threshold", "above-threshold.example"),
        ):
            self._add_range_response(path, host)

        for path, host in (
            ("allowed", "allowed.example"),
            ("above-threshold", "above-threshold.example"),
        ):
            self._server.addResponse(
                "sessionlog.json",
                {
                    "headers": f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nAccept: */*\r\n\r\n",
                    "timestamp": "1469733493.993",
                    "body": "",
                },
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=600\r\nContent-Length: 10\r\n\r\n",
                    "timestamp": "1469733493.993",
                    "body": "hellohello",
                },
            )

    def _configure_ats(self):
        small_config = self._ts.Disk.MakeConfigFile("background_fetch_small.config")
        # Content-Length comparison operators are inclusive: '<' means <= and '>' means >=.
        small_config.AddLine("exclude Content-Length <1000")

        above_threshold_config = self._ts.Disk.MakeConfigFile("background_fetch_above_threshold.config")
        above_threshold_config.AddLine("exclude Content-Length >1000")

        wildcard_config = self._ts.Disk.MakeConfigFile("background_fetch_wildcard.config")
        wildcard_config.AddLine("exclude X-Skip-Bg *")

        self._ts.Disk.remap_config.AddLines(
            [
                f"map http://allowed.example/ http://127.0.0.1:{self._server.Variables.Port}/ @plugin=background_fetch.so",
                f"map http://small.example/ http://127.0.0.1:{self._server.Variables.Port}/ "
                "@plugin=background_fetch.so @pparam=background_fetch_small.config",
                f"map http://wildcard.example/ http://127.0.0.1:{self._server.Variables.Port}/ "
                "@plugin=background_fetch.so @pparam=background_fetch_wildcard.config",
                f"map http://above-threshold.example/ http://127.0.0.1:{self._server.Variables.Port}/ "
                "@plugin=background_fetch.so @pparam=background_fetch_above_threshold.config",
            ])

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'background_fetch',
            })

        self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(
            r"adding background_fetch content length rule .* for Content-Length: 1000",
            "Content-Length exclusion rules should be parsed")
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"found exclude rule match", "expected at least one background_fetch exclusion rule match")
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"Found X-Skip-Bg wild card", "wildcard request header rule should match on presence regardless of value")
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r"Starting background fetch, replaying:", "at least one permitted request should start a background fetch")
        for marker in ("allowed", "above-threshold"):
            self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
                rf"X-Background-Fetch-Test: {marker}", f"{marker} request should start a background fetch")
        for marker in ("small", "wildcard"):
            self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
                rf"X-Background-Fetch-Test: {marker}", f"{marker} request should not start a background fetch")

    def _add_curl_run(self, name, host, path, marker, extra_headers=""):
        tr = Test.AddTestRun(name)
        tr.MakeCurlCommand(
            f'-s -D /dev/stdout -x localhost:{self._ts.Variables.port} -H "Range: bytes=0-4" '
            f'-H "X-Background-Fetch-Test: {marker}" {extra_headers} http://{host}/{path}',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
            "206 Partial Content", f"{name} should receive the partial response")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter += self._server
        return tr

    def _wait_for_background_fetch(self):
        tr = Test.AddTestRun("wait for background fetch log")
        tr.Processes.Default.Command = "echo waiting for background fetch"
        tr.Processes.Default.ReturnCode = 0
        waiter = tr.Processes.Process("await_background_fetch", "sleep 30")
        waiter.Ready = When.FileContains(self._ts.Disk.traffic_out.Name, "X-Background-Fetch-Test: above-threshold")
        tr.Processes.Default.StartBefore(waiter)
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter += self._server

    def run(self):
        allowed_run = self._add_curl_run("allowed background fetch", "allowed.example", "allowed", "allowed")
        allowed_run.Processes.Default.StartBefore(self._server, ready=When.PortOpen(self._server.Variables.Port))
        allowed_run.Processes.Default.StartBefore(self._ts)

        self._add_curl_run("content-length excluded background fetch", "small.example", "small", "small")

        self._add_curl_run(
            "wildcard header excluded background fetch", "wildcard.example", "wildcard", "wildcard", '-H "X-Skip-Bg: yes"')

        self._add_curl_run(
            "content-length permitted background fetch", "above-threshold.example", "above-threshold", "above-threshold")

        self._wait_for_background_fetch()


BackgroundFetchRuleTest().run()
