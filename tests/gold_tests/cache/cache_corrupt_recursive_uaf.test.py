'''
Recursive cache-read tracking must not use-after-free on failure.

This autest sets ATS_TEST_FORCE_CORRUPT_DOC=1 in the ATS environment, which
makes openReadStartEarliest treat every doc magic as corrupt. Reading a
multi-fragment object then drives the recursive earliest-read path: each level
re-enters openReadStartEarliest via do_read_call, and when an inner level falls
into free_CacheVC the outer frame -- on the *unfixed* code -- decrements the
recursion counter through the now-freed CacheVC member. A sanitizer build
catches that as a use-after-free. Note the depth limit ("Too many recursive
calls") is NOT required to trigger the bug: the dangling decrement happens at
whatever level the inner call frees `this`.

With the fix in place the counter lives in a thread_local that outlives any
freed CacheVC, so the recursive run completes cleanly and the proxy stays up.

NOTE ON COVERAGE: the use-after-free is only *caught* when ATS is built with a
memory sanitizer (e.g. the ASan preset the security CI uses). On a non-sanitized
build the dangling decrement is typically a silent no-op, so this test would
pass even on the unfixed sources. What the test verifies unconditionally is that
the forced-corrupt earliest-read path actually runs ("Doc magic does not match")
and that the proxy never crashes (no FATAL). Run it under a sanitizer build to
get the regression-catching guarantee.
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

Test.Summary = 'Recursive cache read must not use-after-free when every doc magic is corrupt'

# The corrupt-doc env hook is compiled in only when TS_HAS_TESTS is enabled;
# without it the hook is a constexpr false and this test would silently exercise
# a normal cache hit instead of the recursive corrupt-doc path.
Test.SkipUnless(Condition.HasATSFeature('TS_HAS_TESTS'))

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")

# The corrupt-doc hook lives in openReadStartEarliest, which is only reached when
# the cached object spans more than one fragment (a single-fragment object is
# served entirely from openReadStartHead and never enters the recursive earliest
# read path). Use a multi-fragment body together with a small target_fragment_size
# below so the second read actually drives openReadStartEarliest. 64 KiB over an
# 8 KiB fragment size is several fragments while keeping the test lightweight.
body = "x" * (64 * 1024)

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /obj HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Length: {0}\r\nCache-Control: max-age=3600\r\n\r\n".format(len(body)),
        "timestamp": "1",
        "body": body
    })

ts = Test.MakeATSProcess("ts", enable_cache=True)
# Force the corrupt-doc path inside openReadStartEarliest for every cache read.
ts.Env['ATS_TEST_FORCE_CORRUPT_DOC'] = '1'

ts.Disk.records_config.update(
    {
        'proxy.config.http.wait_for_cache': 1,
        # Force the object to be written as several small fragments so the read
        # path enters openReadStartEarliest (sizeof(Doc) < value <= cap). Debug
        # logging is intentionally left off: the corruption/recursion diagnostics
        # asserted below are Warning/Error level and always reach diags.log, and
        # cache_read debug over many fragments would bloat the run's disk use.
        'proxy.config.cache.target_fragment_size': 8192,
    })

ts.Disk.remap_config.AddLine('map http://example.com/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

# Assert the forced-corrupt earliest-read path actually ran. The depth-limit
# diagnostic ("Too many recursive calls") is deliberately NOT required: the
# recursion frees `this` at whatever level the inner read fails, which is the
# use-after-free locus, and it does not need to reach MAX_READ_RECURSION_DEPTH.
# So the reliable, sanitizer-independent signal that the vulnerable path was
# exercised is the corruption warning itself. The real regression guard is a
# sanitizer build catching the UAF on the unfixed sources (see module docstring).
# Assigning Content with `=` replaces the framework's default ERROR:/FATAL:
# guards, so re-add a FATAL: guard to keep asserting the proxy never crashed.
ts.Disk.diags_log.Content = Testers.ContainsExpression("Doc magic does not match", "the forced-corrupt earliest-read path must run")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL:", "ATS must not crash on the recursive corrupt-doc path")

# First request: cache miss, populate cache from origin.
tr1 = Test.AddTestRun()
tr1.MakeCurlCommandMulti(
    '{curl} -sS -i -x 127.0.0.1:TSPORT http://example.com/obj'.replace('TSPORT', str(ts.Variables.port)), ts=ts)
tr1.Processes.Default.StartBefore(ts)
tr1.Processes.Default.StartBefore(server)
tr1.Processes.Default.ReturnCode = 0
# The populate read also walks the forced-corrupt earliest path; assert ATS and
# origin survive it (a UAF crash here would otherwise look like a later failure).
tr1.StillRunningAfter = ts
tr1.StillRunningAfter = server

# Second request: cache hit; openReadStartEarliest treats the doc as corrupt
# (env var) and drives the recursive earliest-read path. With the fix ATS
# survives; the unfixed code would be caught by a sanitizer as UAF on the
# recursion counter.
tr2 = Test.AddTestRun()
tr2.MakeCurlCommandMulti(
    '{curl} -sS -i -x 127.0.0.1:TSPORT http://example.com/obj'.replace('TSPORT', str(ts.Variables.port)), ts=ts)
tr2.Processes.Default.ReturnCode = 0
# Distinguishing assertion: ATS must remain up and respond. Body content
# doesn't matter; what matters is the proxy didn't crash.
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1", "ATS must respond, not crash")
# Belt-and-suspenders: the proxy (and origin) must still be alive after serving
# the recursive corrupt-doc read -- catches a crash that lands right after the
# response is written.
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
