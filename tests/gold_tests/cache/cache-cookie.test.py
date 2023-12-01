'''
Test cookie-related caching behaviors
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
Test cookie-related caching behaviors
'''

Test.ContinueOnFail = True

# **testname is required**
testName = ""


class CookieDefaultTest:
    # Verify the correct caching behavior when ATS is in default configuration
    cookieDefaultReplayFile = "replay/cookie-default.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("cookie-default-verifier-server", self.cookieDefaultReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-cookie-default")
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun("Verify the correct caching behavior when ATS is in default configuration")
        tr.AddVerifierClientProcess(
            "cookie-default-client",
            self.cookieDefaultReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


CookieDefaultTest().run()


class CookieBypassTest:
    # Verify the correct caching behavior when ATS is configured to not cache
    # response to cookie for any content type
    cookieBypassReplayFile = "replay/cookie-bypass-cache.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("cookie-bypass-verifier-server", self.cookieBypassReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-cookie-bypass")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Bypass cache for any responses to cookies
                "proxy.config.http.cache.cache_responses_to_cookies": 0
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the correct caching behavior when ATS is configured to not cache response to cookie for any content type")
        tr.AddVerifierClientProcess(
            "cookie-bypass-client", self.cookieBypassReplayFile, http_ports=[self.ts.Variables.port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


CookieBypassTest().run()


class CookieImgOnlyTest:
    # Verify the correct caching behavior when ATS is configured to cache
    # response to cookie only for image content type
    cookieImgOnlyReplayFile = "replay/cookie-cache-img-only.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("cookie-img-only-verifier-server", self.cookieImgOnlyReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-cookie-img-only")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Cache only for image types
                "proxy.config.http.cache.cache_responses_to_cookies": 2
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the correct caching behavior when ATS is configured to cache response to cookie only for image content type")
        tr.AddVerifierClientProcess(
            "cookie-img-only-client",
            self.cookieImgOnlyReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


CookieImgOnlyTest().run()


class CookieAllButTextTest:
    # Verify the correct caching behavior when ATS is configured to cache
    # response to cookie for all but text types
    cookieAllButTextReplayFile = "replay/cookie-all-but-text.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("cookie-all-but-text-verifier-server", self.cookieAllButTextReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-cookie-all-but-text")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Cache all content type except text
                "proxy.config.http.cache.cache_responses_to_cookies": 3
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the correct caching behavior when ATS is configured to cache response to cookie for all but text types")
        tr.AddVerifierClientProcess(
            "cookie-all-but-text-client",
            self.cookieAllButTextReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


CookieAllButTextTest().run()


class CookieAllButTextWithExcpTest:
    # Verify the correct caching behavior when ATS is configured to cache all
    # content types but text, but with a few exceptions for text types which
    # would also be cached
    cookieAllButTextReplayFile = "replay/cookie-all-but-text-with-excp.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            "cookie-all-but-text-with-excp-verifier-server", self.cookieAllButTextReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-cookie-all-but-text-with-excp")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Cache all content type but text. Text type also gets cached for
                # server responses without Set-Cookie or with Cache-Control: public
                "proxy.config.http.cache.cache_responses_to_cookies": 4
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the correct caching behavior when ATS is configured to cache all content types but text, but with a few exceptions for text types which would also be cached"
        )
        tr.AddVerifierClientProcess(
            "cookie-all-but-text-with-excp-client",
            self.cookieAllButTextReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


CookieAllButTextWithExcpTest().run()
