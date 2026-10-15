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
import json

Test.Summary = '''
Test regex_remap
'''

# Test description:
# Exercise regex_remap rule matching, redirects, pristine-URL mapping, and the
# regex match limit. Then verify that two map rules naming the same rule file
# share one compiled rule set, and that rewriting that file and reloading
# remap.config compiles a new shared generation instead of reusing the live one.
# No rule here uses a $n / $h substitution, so that path is not covered.

Test.SkipUnless(Condition.PluginExists('regex_remap.so'),)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%uuid}")
server.addSessionFromFiles("replay")
replay = {}
with open(os.path.join(Test.TestDirectory, 'replay/yts-2819.replay.json')) as src:
    replay = json.load(src)

replay_txns = replay["sessions"][0]["transactions"]

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

# Define ATS and configure
ts = Test.MakeATSProcess("ts", enable_cache=False)

# These two rules deliberately exercise resource limits. Replace the blanket
# error exclusion once, then append independent checks for each rule below.
ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    r'ERROR: (?!\[regex_remap\] Bad regular expression result '
    r'(?:-(?:46|47|53|63) .* from "\^/alpha/bravo/|-47 .* from "\^/match_limit/))',
    "Only the deliberate resource-limit errors are allowed")

testName = "regex_remap"

regex_remap_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_remap.conf')
regex_remap2_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_remap2.conf')
curl_and_args = '-s -D - -v --proxy localhost:{} '.format(ts.Variables.port)

regex_remap_lines = [
    "# regex_remap configuration\n",
    "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ https://redirect.com/ @status=301\n",
    "^/match_limit/(a+)+$ https://redirect.com/ @status=301\n",
]

ts.Disk.File(regex_remap_conf_path, typename="ats:config").AddLines(regex_remap_lines)

ts.Disk.File(
    regex_remap2_conf_path, typename="ats:config").AddLines(
        [
            "# 2nd regex_remap configuration\n"
            "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ " + f"http://localhost:{server.Variables.Port}\n"
        ])

ts.Disk.remap_config.AddLine(
    "map http://example.one/ http://localhost:{}/ @plugin=regex_remap.so @pparam=regex_remap.conf\n".format(server.Variables.Port))
ts.Disk.remap_config.AddLine(
    "map http://example.two/ http://localhost:{}/ ".format(server.Variables.Port) +
    "@plugin=regex_remap.so @pparam=regex_remap.conf @pparam=pristine\n")
ts.Disk.remap_config.AddLine(
    "map http://example.three/ http://wrong.com/ ".format(server.Variables.Port) +
    "@plugin=regex_remap.so @pparam=regex_remap2.conf @pparam=pristine\n")

# The cache assertions below depend on regex_remap remaining in the debug tags.
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|regex_remap',
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL',
        # Run 3b sends a query past the 1 MB JIT stack's ~44 KB bound, which is over the
        # 32 KB default. Do not raise request_line_max_size to match: http_parser_parse_req
        # asserts `parsed.size() < UINT16_MAX` before it rejects an over-long line, so on
        # any assert-enabled build a request line past 65,535 aborts traffic_server instead
        # of getting a 414. 65,535 is the real ceiling here, whatever the record says.
        'proxy.config.http.request_header_max_size': 131072
    })

# 0 Test - Load cache (miss) (path1)
tr = Test.AddTestRun("smoke test")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(nameserver)
tr.Processes.Default.StartBefore(Test.Processes.ts)
creq = replay_txns[0]['client-request']
tr.MakeCurlCommand(curl_and_args + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + creq["url"], ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_smoke.gold"
tr.StillRunningAfter = ts

# 1 Test - Match and redirect
tr = Test.AddTestRun("pristine test")
tr.MakeCurlCommand(
    curl_and_args + "'http://example.two/alpha/bravo/?action=newsfed;param0001=00003E;param0002=00004E;param0003=00005E'" +
    f" | grep -e '^HTTP/' -e '^Location' | sed 's/{server.Variables.Port}/SERVER_PORT/'",
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_redirect.gold"
tr.StillRunningAfter = ts

# 2 Test - Match and remap
tr = Test.AddTestRun("2nd pristine test")
tr.MakeCurlCommand(
    curl_and_args + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) +
    " 'http://example.three/alpha/bravo/?action=newsfed;param0001=00003E;param0002=00004E;param0003=00005E'" +
    " | grep -e '^HTTP/' -e '^Content-Length'",
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_simple.gold"
tr.StillRunningAfter = ts

# 3 Test - A 3 KB query redirects. This rule backtracks once per subject
# character, so it used to exhaust the 32 KB stack PCRE2 falls back to when a
# match context carries none, and the rule was skipped. The plugin's context now
# inherits the shared 1 MB stack, so the rule matches and the redirect fires.
#
# This run needs no JIT gate. With JIT the 1 MB stack resolves the subject, and
# without JIT the interpreter resolves it on the heap, so the redirect is the
# answer either way. It only demonstrates the fix on a build that has a JIT.
tr = Test.AddTestRun("long query redirects rather than exhausting the JIT stack")
creq = replay_txns[1]['client-request']
tr.MakeCurlCommand(
    curl_and_args + f"--header 'uuid: {creq['headers']['fields'][1][1]}' '{creq['url']}'" + " | grep -e '^HTTP/' -e '^Location'",
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_redirect.gold"
tr.StillRunningAfter = ts

# 3b Test - Preserve the original crash guard from #5762. This request must
# survive resource exhaustion without redirecting, regardless of which matching
# resource limit is reached (JIT stack, match work, depth, or heap). Against the
# shared 1 MB stack this rule needs a subject past ~44 KB to exhaust it (measured:
# 43,514 matches, 44,021 does not), which is why the header limit is raised above.
# Shortening this query silently turns the run into a plain redirect test.
#
# This run is boxed in, and the box cannot be widened by configuration. The floor is
# that ~44 KB bound; the ceiling is the 65,535 request line the parser asserts on. So
# the usable window is roughly 44,100 to 65,475 bytes of query, about 1.5x wide, and
# 64,000 is near the top of it. A platform whose JIT frames are enough smaller to push
# the floor over the ceiling cannot run this case at any query length: prefer the
# test_Regex.cc unit test, which bounds the stack directly, over stretching this one.
#
# Note this run is a crash guard, not a check on the stack size: its ContainsExpression
# accepts any of -46/-47/-53/-63, and the 32 KB fallback reaches -46 too. Run 3 above is
# what actually fails if the fix is reverted, because its 3 KB subject redirects on the
# 1 MB stack and errors on the fallback.
#
# Only the JIT engine has a stack to exhaust here. PCRE2's interpreter keeps its
# backtracking frames on the heap, so on a build without JIT this subject simply
# matches, the rule redirects, and both assertions below fail for a reason that has
# nothing to do with the behaviour under test. The crash property itself is not lost
# on such a build: run 4 reaches the match limit through the interpreter, and the
# unit test in test_Regex.cc asserts it directly.
if Condition.HasATSFeature('TS_HAS_PCRE2_JIT'):
    crash_guard_query = 'x' * 64000
    tr = Test.AddTestRun("resource exhaustion does not crash ATS")
    tr.MakeCurlCommand(
        curl_and_args + "--header 'uuid: 180' " + f"'http://example.one/alpha/bravo/?action=newsfed;{crash_guard_query}'", ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "gold/regex_remap_crash.gold"
    ts.Disk.diags_log.Content += Testers.ContainsExpression(
        r'ERROR: \[regex_remap\] Bad regular expression result -(?:46|47|53|63).*"\^/alpha/bravo/',
        "The crash-guard rule must report resource exhaustion")
    tr.StillRunningAfter = ts

# 4 Test - The nested quantifiers must exceed PCRE2's default matching-work limit.
tr = Test.AddTestRun("excessive backtracking reaches the match limit")
creq = replay_txns[2]['client-request']
tr.MakeCurlCommand(curl_and_args + \
    '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + '"{}"'.format(creq["url"]), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_crash.gold"
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    r'ERROR: \[regex_remap\] Bad regular expression result -47.*\^/match_limit/',
    "The excessive-backtracking rule must reach the match limit")
tr.StillRunningAfter = ts


class TestRegexRemapRuleCache:
    '''Verify shared compiled rules across a remap.config reload.'''

    updated_rule = "^/cache-generation$ https://updated.example/ @status=302\n"

    def __init__(self, ts_process: 'Process', original_rules: str, curl_args: str):
        '''Configure the cache and reload TestRuns.'''
        self._ts = ts_process
        self._original_rules = original_rules
        self._curl_args = curl_args
        self._regex_remap_path = os.path.join(ts_process.Variables.CONFIGDIR, 'regex_remap.conf')
        self._remap_path = os.path.join(ts_process.Variables.CONFIGDIR, 'remap.config')

        self._add_rule_update_run()
        self._add_reload_run()
        self._add_new_generation_run()
        self._add_shared_generation_run()
        self._add_isolated_generation_run()
        self._add_cache_verification_run()

    def _update_rules(self) -> None:
        '''Write a new rule generation and mark remap.config as changed.'''
        with open(self._regex_remap_path, 'w') as config_file:
            config_file.write(self.updated_rule + self._original_rules)
        os.utime(self._remap_path)

    def _add_rule_update_run(self) -> 'TestRun':
        '''Change the shared rule file while its first generation is live.'''
        tr = Test.AddTestRun("change shared regex_remap rules")
        tr.Processes.Default.Command = "echo 'Updating shared regex_remap rules'"
        tr.Processes.Default.Setup.Lambda(self._update_rules)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts
        return tr

    def _add_reload_run(self) -> 'TestRun':
        '''Reload remap.config after the shared rule file changes.'''
        tr = Test.AddConfigReload(self._ts, expect_tasks=["remap.config"], description="Reload changed shared regex_remap rules")
        tr.StillRunningAfter = self._ts
        return tr

    def _add_new_generation_run(self) -> 'TestRun':
        '''Verify the new rule generation is active after the reload.'''
        tr = Test.AddTestRun("new shared rule generation")
        tr.MakeCurlCommand(self._curl_args + "'http://example.one/cache-generation' | grep -e '^HTTP/' -e '^Location'", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 302", "New rule returns a redirect")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "Location: https://updated.example/", "New rule generation is active")
        tr.StillRunningAfter = self._ts
        return tr

    def _add_shared_generation_run(self) -> 'TestRun':
        '''Verify the other mapping on this file sees the same generation.'''
        tr = Test.AddTestRun("second mapping sees same rule generation")
        tr.MakeCurlCommand(self._curl_args + "'http://example.two/cache-generation' | grep -e '^HTTP/' -e '^Location'", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 302", "Sharing mapping returns a redirect")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "Location: https://updated.example/", "Sharing mapping is on the new generation")
        tr.StillRunningAfter = self._ts
        return tr

    def _add_isolated_generation_run(self) -> 'TestRun':
        '''Verify a different rule file does not reuse the changed generation.'''
        tr = Test.AddTestRun("different rule file remains isolated")
        tr.MakeCurlCommand(
            self._curl_args + "'http://example.three/cache-generation' | grep -e '^HTTP/' -e '^Location'", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("HTTP/1.1 302", "Different file does not redirect")
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            "Location: https://updated.example/", "Different file does not use the changed generation")
        tr.StillRunningAfter = self._ts
        return tr

    def _add_cache_verification_run(self) -> 'TestRun':
        '''Verify each distinct generation is compiled only once.'''
        await_tr = Test.AddAwaitFileContainsTestRun(
            "await rule cache debug output", self._ts.Disk.traffic_out.Name, "Reusing cached regular expressions from", 3)
        await_tr.StillRunningAfter = self._ts

        # Compiles: regex_remap.conf gen1, regex_remap2.conf gen1, and
        # regex_remap.conf gen2 == 3 generations and 6 regular expressions.
        # Reuses: example.two shares regex_remap.conf on both loads, while
        # example.three reuses regex_remap2.conf across the build-then-swap
        # reload because the previous remap table is still holding it == 3.
        tr = Test.AddTestRun("verify compiled rule cache")
        tr.Processes.Default.Command = (
            f"log={self._ts.Disk.traffic_out.Name}; "
            "cached=$$(grep -c 'Cached regular expressions from' $$log); "
            "reused=$$(grep -c 'Reusing cached regular expressions from' $$log); "
            "compiled=$$(grep -c 'Compiling regex:' $$log); "
            "cached_primary=$$(grep 'Cached regular expressions from' $$log | grep -F -c '/regex_remap.conf'); "
            "cached_secondary=$$(grep 'Cached regular expressions from' $$log | grep -F -c '/regex_remap2.conf'); "
            "reused_primary=$$(grep 'Reusing cached regular expressions from' $$log | grep -F -c '/regex_remap.conf'); "
            "reused_secondary=$$(grep 'Reusing cached regular expressions from' $$log | grep -F -c '/regex_remap2.conf'); "
            "echo cached=$$cached reused=$$reused compiled=$$compiled "
            "cached_primary=$$cached_primary cached_secondary=$$cached_secondary "
            "reused_primary=$$reused_primary reused_secondary=$$reused_secondary; "
            "test $$cached -eq 3 -a $$reused -eq 3 -a $$compiled -eq 6 -a "
            "$$cached_primary -eq 2 -a $$cached_secondary -eq 1 -a "
            "$$reused_primary -eq 2 -a $$reused_secondary -eq 1")
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts
        return tr


TestRegexRemapRuleCache(ts, ''.join(regex_remap_lines), curl_and_args)
