'''
Test cache write lock contention behavior with parallel requests for actions 5 and 6.

This test issues multiple parallel curl requests to trigger actual cache write lock
contention. It is skipped by default because the behavior is timing-sensitive and
not suitable for CI environments.

To run this test manually:
    export RUN_CACHE_CONTENTION_TEST=1
    cd build/tests
    ./autest.sh --sandbox /tmp/sbcursor --clean=none -f cache-write-lock-contention
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
Test cache write lock contention with parallel requests (actions 5 and 6).

This test is SKIPPED by default. Set RUN_CACHE_CONTENTION_TEST=1 to run it.
'''

Test.ContinueOnFail = True

Test.SkipUnless(
    Condition(
        lambda: os.environ.get('RUN_CACHE_CONTENTION_TEST', '').lower() in ('1', 'true', 'yes'),
        "Set RUN_CACHE_CONTENTION_TEST=1 to run this timing-sensitive test", True))


def make_parallel_curl(url, count, stagger_delay=None):
    """
    Generate a parallel curl command string.

    Args:
        url: The URL to request
        count: Number of parallel requests
        stagger_delay: If set, add this delay between requests (e.g., "0.1")
                      If None, all requests fire simultaneously
    """
    parts = ['(']
    for i in range(1, count + 1):
        if i > 1 and stagger_delay:
            parts.append(f'sleep {stagger_delay} && ')
        parts.append(
            f'{{curl}} -s -o /dev/null -w "req{i}: %{{{{http_code}}}}\\n" "{url}" '
            f'-H "Host: example.com" -H "X-Request: req{i}" & ')
    parts.append('wait)')
    return ''.join(parts)


class ContentionTest:
    """
    Base class for cache write lock contention tests.

    Tests parallel requests to trigger cache write lock contention and verify
    that the configured fail_action behaves correctly.
    """

    def __init__(self, name, action, description, stale_scenario=False):
        self.name = name
        self.action = action
        self.description = description
        self.stale_scenario = stale_scenario
        self._setup()

    def _setup(self):
        origin_delay = 5 if self.stale_scenario else 3
        self.server = Test.MakeOriginServer(f"server_{self.name}", delay=origin_delay)

        max_age = 1 if self.stale_scenario else 300
        self.server.addResponse(
            "sessionlog.json", {
                "headers": f"GET /test-{self.name} HTTP/1.1\r\nHost: example.com\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    f"HTTP/1.1 200 OK\r\n"
                    f"Content-Length: 100\r\n"
                    f"Cache-Control: max-age={max_age}\r\n"
                    f"X-Origin: {self.name}\r\n"
                    f"Connection: close\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "X" * 100
            })

        self.ts = Test.MakeATSProcess(f"ts_{self.name}", enable_cache=True)

        read_retries = 2 if self.stale_scenario else 10
        retry_time = 100 if self.stale_scenario else 500

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|cache|http_cache|http_trans',
                'proxy.config.http.cache.open_write_fail_action': self.action,
                'proxy.config.http.cache.max_open_write_retries': 1,
                'proxy.config.http.cache.max_open_write_retry_timeout': 0,
                'proxy.config.http.cache.max_open_read_retries': read_retries,
                'proxy.config.http.cache.open_read_retry_time': retry_time,
                'proxy.config.cache.enable_read_while_writer': 1,
                'proxy.config.http.cache.max_stale_age': 300,
            })
        self.ts.Disk.remap_config.AddLine(f'map http://example.com/ http://127.0.0.1:{self.server.Variables.Port}/')

    def run(self):
        url_path = f"/test-{self.name}"
        url = f"http://127.0.0.1:{self.ts.Variables.port}{url_path}"

        if self.stale_scenario:
            tr = Test.AddTestRun(f"{self.description} - Prime cache")
            tr.Processes.Default.StartBefore(self.server)
            tr.Processes.Default.StartBefore(self.ts)
            tr.MakeCurlCommand(f'-s -D - "{url}" -H "Host: example.com"', ts=self.ts)
            tr.Processes.Default.ReturnCode = 0
            tr.StillRunningAfter = self.ts
            tr.StillRunningAfter = self.server

            tr = Test.AddTestRun(f"{self.description} - Wait for stale")
            tr.Processes.Default.Command = "sleep 3"
            tr.Processes.Default.ReturnCode = 0
            tr.StillRunningAfter = self.ts
            tr.StillRunningAfter = self.server

        tr = Test.AddTestRun(self.description)
        if not self.stale_scenario:
            tr.Processes.Default.StartBefore(self.server)
            tr.Processes.Default.StartBefore(self.ts)

        count = 8 if self.stale_scenario else 5
        stagger = None if self.stale_scenario else "0.1"
        tr.MakeCurlCommandMulti(make_parallel_curl(url, count, stagger), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

        tr = Test.AddTestRun(f"{self.description} - Verify cache")
        tr.MakeCurlCommand(f'-s -D - "{url}" -H "Host: example.com"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200 OK", "Cache serves 200")
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

        self.ts.Disk.traffic_out.Content = Testers.ExcludesExpression("FATAL|ink_release_assert|ink_abort", "No crashes")

        if self.stale_scenario and self.action == 6:
            self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
                "serving stale \\(action 6\\)|object stale, serving stale", "Stale fallback triggered")
        else:
            self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
                "cache open read failure.*retrying|read while write", "Cache contention occurred")


# Action 5: READ_RETRY - retries cache reads, goes to origin if exhausted
ContentionTest("a5_fresh", 5, "Action 5 - fresh contention").run()
ContentionTest("a5_stale", 5, "Action 5 - stale revalidation", stale_scenario=True).run()

# Action 6: READ_RETRY_STALE_ON_REVALIDATE - like action 5, but serves stale if available
ContentionTest("a6_fresh", 6, "Action 6 - fresh contention").run()
ContentionTest("a6_stale", 6, "Action 6 - stale fallback", stale_scenario=True).run()
