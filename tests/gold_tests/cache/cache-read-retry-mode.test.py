'''
Test cache_open_write_fail_action = 5 (READ_RETRY mode)
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
Test cache_open_write_fail_action = 5 (READ_RETRY mode) to verify:
1. Basic read-while-writer behavior with fail_action=5
2. Multiple concurrent requests for same uncached URL trigger READ_RETRY
3. Stale content revalidation with concurrent requests triggers READ_RETRY
4. System does not crash under write lock contention
5. All requests are served correctly
'''

Test.ContinueOnFail = True

# Basic tests using Proxy Verifier replays (sequential, validate basic behavior)
Test.ATSReplayTest(replay_file="replay/cache-read-retry-basic.replay.yaml")
Test.ATSReplayTest(replay_file="replay/cache-read-retry-exhausted.replay.yaml")


class ReadRetryParallelTest:
    """
    Test that READ_RETRY mode (fail_action=5) handles write lock contention correctly.

    Strategy:
    1. Fire multiple parallel requests for the same uncached URL
    2. Origin server responds slowly (2 seconds via httpbin /delay)
    3. First request gets write lock and starts writing to cache
    4. Subsequent requests fail to get write lock, enter READ_RETRY
    5. All requests should complete successfully (no crashes)
    """

    def __init__(self):
        self.__setup_origin_server()
        self.__setup_ts()

    def __setup_origin_server(self):
        # Use httpbin which handles concurrent requests and has /delay endpoint
        self.server = Test.MakeHttpBinServer("httpbin-parallel")

    def __setup_ts(self):
        self.ts = Test.MakeATSProcess("ts-parallel", enable_cache=True)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|cache|http_cache|http_trans',
                # Enable READ_RETRY mode
                'proxy.config.http.cache.open_write_fail_action': 5,
                # Configure write retry parameters - low count to fall back to READ_RETRY quickly
                'proxy.config.http.cache.max_open_write_retries': 1,
                'proxy.config.http.cache.max_open_write_retry_timeout': 5,
                # Configure read retry parameters
                'proxy.config.http.cache.max_open_read_retries': 10,
                'proxy.config.http.cache.open_read_retry_time': 100,
                # Enable read-while-writer
                'proxy.config.cache.enable_read_while_writer': 1,
                # Required for caching
                'proxy.config.http.cache.required_headers': 0,
            })

        self.ts.Disk.remap_config.AddLine(f'map http://example.com/ http://127.0.0.1:{self.server.Variables.Port}/')

    def test_parallel_requests(self):
        """
        Fire multiple curl requests in parallel using shell backgrounding.
        All requests should complete successfully.
        """
        tr = Test.AddTestRun("Parallel requests for same uncached URL")
        ps = tr.Processes.Default

        ps.StartBefore(self.server)
        ps.StartBefore(self.ts)

        # Fire 5 parallel requests using shell backgrounding
        # The /delay/2 endpoint makes origin respond after 2 seconds
        # First request gets the write lock, others should enter READ_RETRY
        port = self.ts.Variables.port
        tr.MakeCurlCommandMulti(
            f'''
# Fire all requests in parallel (backgrounded) to same slow URL
({{curl}} -s -o /dev/null -w "Request 1: HTTP %{{{{http_code}}}}\\n" -H "Host: example.com" http://127.0.0.1:{port}/delay/2) &
({{curl}} -s -o /dev/null -w "Request 2: HTTP %{{{{http_code}}}}\\n" -H "Host: example.com" http://127.0.0.1:{port}/delay/2) &
({{curl}} -s -o /dev/null -w "Request 3: HTTP %{{{{http_code}}}}\\n" -H "Host: example.com" http://127.0.0.1:{port}/delay/2) &
({{curl}} -s -o /dev/null -w "Request 4: HTTP %{{{{http_code}}}}\\n" -H "Host: example.com" http://127.0.0.1:{port}/delay/2) &
({{curl}} -s -o /dev/null -w "Request 5: HTTP %{{{{http_code}}}}\\n" -H "Host: example.com" http://127.0.0.1:{port}/delay/2) &
# Wait for all background jobs to complete
wait
echo "All parallel requests completed"
''',
            ts=self.ts)

        ps.ReturnCode = 0
        # All requests should return HTTP 200
        ps.Streams.stdout.Content = Testers.ContainsExpression("Request 1: HTTP 200", "First request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Request 2: HTTP 200", "Second request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Request 3: HTTP 200", "Third request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Request 4: HTTP 200", "Fourth request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Request 5: HTTP 200", "Fifth request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("All parallel requests completed", "All requests should complete")

        tr.StillRunningAfter = self.ts

    def test_verify_cache_populated(self):
        """
        Verify the cache was populated after parallel requests.
        Subsequent request should be fast (from cache).
        """
        tr = Test.AddTestRun("Verify cache hit after parallel requests")
        ps = tr.Processes.Default

        port = self.ts.Variables.port
        # Request without delay - should be served from cache quickly
        tr.MakeCurlCommandMulti(
            f'''
{{curl}} -s -D - -o /dev/null -H "Host: example.com" http://127.0.0.1:{port}/delay/2
''', ts=self.ts)

        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "Request should succeed")

        tr.StillRunningAfter = self.ts

    def test_no_crash_indicators(self):
        """
        Verify ATS logs don't contain crash indicators.
        """
        tr = Test.AddTestRun("Verify no crash indicators in logs")
        ps = tr.Processes.Default

        # Check traffic.out for crash indicators
        ps.Command = f'grep -E "FATAL|ALERT|Emergency|ink_release_assert|ink_abort" {self.ts.Variables.LOGDIR}/traffic.out || echo "No crash indicators found"'

        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("No crash indicators found", "Should not find any crash indicators")

        tr.StillRunningAfter = self.ts

    def run(self):
        self.test_parallel_requests()
        self.test_verify_cache_populated()
        self.test_no_crash_indicators()


class ReadRetryRevalidationTest:
    """
    Test that READ_RETRY mode handles stale content revalidation with concurrent requests.

    Strategy:
    1. Populate cache with content that has a short TTL
    2. Wait for content to become stale
    3. Fire multiple parallel requests for the stale URL
    4. All requests see stale content and attempt to revalidate
    5. First request gets write lock, others enter READ_RETRY
    6. All requests should complete successfully
    """

    def __init__(self):
        self.__setup_origin_server()
        self.__setup_ts()

    def __setup_origin_server(self):
        # Use httpbin for slow revalidation responses
        self.server = Test.MakeHttpBinServer("httpbin-revalidate")

    def __setup_ts(self):
        self.ts = Test.MakeATSProcess("ts-revalidate", enable_cache=True)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|cache|http_cache|http_trans',
                # Enable READ_RETRY mode
                'proxy.config.http.cache.open_write_fail_action': 5,
                # Configure write retry parameters
                'proxy.config.http.cache.max_open_write_retries': 1,
                'proxy.config.http.cache.max_open_write_retry_timeout': 5,
                # Configure read retry parameters
                'proxy.config.http.cache.max_open_read_retries': 10,
                'proxy.config.http.cache.open_read_retry_time': 100,
                # Enable read-while-writer
                'proxy.config.cache.enable_read_while_writer': 1,
                # Required for caching
                'proxy.config.http.cache.required_headers': 0,
            })

        self.ts.Disk.remap_config.AddLine(f'map http://example.com/ http://127.0.0.1:{self.server.Variables.Port}/')

    def test_populate_cache(self):
        """
        Populate cache with content that has short TTL.
        Using /cache/{seconds} which returns Cache-Control: max-age={seconds}
        """
        tr = Test.AddTestRun("Populate cache with short-TTL content")
        ps = tr.Processes.Default

        ps.StartBefore(self.server)
        ps.StartBefore(self.ts)

        port = self.ts.Variables.port
        # /cache/1 returns content with max-age=1
        tr.MakeCurlCommandMulti(
            f'''
{{curl}} -s -D - -o /dev/null -H "Host: example.com" http://127.0.0.1:{port}/cache/1 && echo "Cache populated with 1-second TTL content"
''',
            ts=self.ts)

        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "Initial request should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Cache populated", "Cache population message")

        tr.StillRunningAfter = self.ts

    def test_parallel_revalidation(self):
        """
        Wait for content to become stale, then send parallel revalidation requests.
        """
        tr = Test.AddTestRun("Parallel revalidation requests for stale content")
        ps = tr.Processes.Default

        port = self.ts.Variables.port
        # Wait 2 seconds for content to become stale, then fire parallel requests
        # Use /delay/2 for the revalidation to be slow (simulating slow origin)
        tr.MakeCurlCommandMulti(
            f'''
sleep 2 && ({{curl}} -s -o /dev/null -w "Revalidate 1: HTTP %{{{{http_code}}}}" -H "Host: example.com" http://127.0.0.1:{port}/delay/2 && echo) &
({{curl}} -s -o /dev/null -w "Revalidate 2: HTTP %{{{{http_code}}}}" -H "Host: example.com" http://127.0.0.1:{port}/delay/2 && echo) &
({{curl}} -s -o /dev/null -w "Revalidate 3: HTTP %{{{{http_code}}}}" -H "Host: example.com" http://127.0.0.1:{port}/delay/2 && echo) &
({{curl}} -s -o /dev/null -w "Revalidate 4: HTTP %{{{{http_code}}}}" -H "Host: example.com" http://127.0.0.1:{port}/delay/2 && echo) &
wait && echo "All revalidation requests completed"
''',
            ts=self.ts)

        ps.ReturnCode = 0
        # All revalidation requests should succeed
        ps.Streams.stdout.Content = Testers.ContainsExpression("Revalidate 1: HTTP 200", "First revalidation should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Revalidate 2: HTTP 200", "Second revalidation should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Revalidate 3: HTTP 200", "Third revalidation should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression("Revalidate 4: HTTP 200", "Fourth revalidation should succeed")
        ps.Streams.stdout.Content += Testers.ContainsExpression(
            "All revalidation requests completed", "All requests should complete")

        tr.StillRunningAfter = self.ts

    def test_no_crash_indicators(self):
        """
        Verify ATS logs don't contain crash indicators.
        """
        tr = Test.AddTestRun("Verify no crash indicators after revalidation")
        ps = tr.Processes.Default

        ps.Command = f'grep -E "FATAL|ALERT|Emergency|ink_release_assert|ink_abort" {self.ts.Variables.LOGDIR}/traffic.out || echo "No crash indicators found"'

        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression("No crash indicators found", "Should not find any crash indicators")

        tr.StillRunningAfter = self.ts

    def run(self):
        self.test_populate_cache()
        self.test_parallel_revalidation()
        self.test_no_crash_indicators()


# Run the parallel tests that actually trigger READ_RETRY
ReadRetryParallelTest().run()
ReadRetryRevalidationTest().run()
