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

Test.SkipUnless(
    Condition.PluginExists('regex_remap.so'),
)
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

ts.Disk.File(regex_remap2_conf_path, typename="ats:config").AddLines(
    [
        "# 2nd regex_remap configuration\n"
        "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ " + f"http://localhost:{server.Variables.Port}\n"
    ]
)

ts.Disk.remap_config.AddLine(
    "map http://example.one/ http://localhost:{}/ @plugin=regex_remap.so @pparam=regex_remap.conf\n".format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    "map http://example.two/ http://localhost:{}/ ".format(server.Variables.Port)
    + "@plugin=regex_remap.so @pparam=regex_remap.conf @pparam=pristine\n"
)
ts.Disk.remap_config.AddLine(
    "map http://example.three/ http://wrong.com/ ".format(server.Variables.Port)
    + "@plugin=regex_remap.so @pparam=regex_remap2.conf @pparam=pristine\n"
)

# The cache assertions below depend on regex_remap remaining in the debug tags.
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|regex_remap',
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL',
    }
)

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
    curl_and_args
    + "'http://example.two/alpha/bravo/?action=newsfed;param0001=00003E;param0002=00004E;param0003=00005E'"
    + f" | grep -e '^HTTP/' -e '^Location' | sed 's/{server.Variables.Port}/SERVER_PORT/'",
    ts=ts,
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_redirect.gold"
tr.StillRunningAfter = ts

# 2 Test - Match and remap
tr = Test.AddTestRun("2nd pristine test")
tr.MakeCurlCommand(
    curl_and_args
    + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1])
    + " 'http://example.three/alpha/bravo/?action=newsfed;param0001=00003E;param0002=00004E;param0003=00005E'"
    + " | grep -e '^HTTP/' -e '^Content-Length'",
    ts=ts,
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_simple.gold"
tr.StillRunningAfter = ts

# 3 Test - Match limit test 0
tr = Test.AddTestRun("match limit 0")
creq = replay_txns[1]['client-request']
tr.MakeCurlCommand(
    curl_and_args + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + '"{}"'.format(creq["url"]), ts=ts
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_crash.gold"
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    'ERROR: .regex_remap. Bad regular expression result -47', "Match limit exceeded"
)
tr.StillRunningAfter = ts

# 4 Test - Match limit test 1
tr = Test.AddTestRun("match limit 1")
creq = replay_txns[2]['client-request']
tr.MakeCurlCommand(
    curl_and_args + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + '"{}"'.format(creq["url"]), ts=ts
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_crash.gold"
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    'ERROR: .regex_remap. Bad regular expression result -47', "Match limit exceeded"
)
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
            "Location: https://updated.example/", "New rule generation is active"
        )
        tr.StillRunningAfter = self._ts
        return tr

    def _add_shared_generation_run(self) -> 'TestRun':
        '''Verify the other mapping on this file sees the same generation.'''
        tr = Test.AddTestRun("second mapping sees same rule generation")
        tr.MakeCurlCommand(self._curl_args + "'http://example.two/cache-generation' | grep -e '^HTTP/' -e '^Location'", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 302", "Sharing mapping returns a redirect")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "Location: https://updated.example/", "Sharing mapping is on the new generation"
        )
        tr.StillRunningAfter = self._ts
        return tr

    def _add_isolated_generation_run(self) -> 'TestRun':
        '''Verify a different rule file does not reuse the changed generation.'''
        tr = Test.AddTestRun("different rule file remains isolated")
        tr.MakeCurlCommand(
            self._curl_args + "'http://example.three/cache-generation' | grep -e '^HTTP/' -e '^Location'", ts=self._ts
        )
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("HTTP/1.1 302", "Different file does not redirect")
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            "Location: https://updated.example/", "Different file does not use the changed generation"
        )
        tr.StillRunningAfter = self._ts
        return tr

    def _add_cache_verification_run(self) -> 'TestRun':
        '''Verify each distinct generation is compiled only once.'''
        await_tr = Test.AddAwaitFileContainsTestRun(
            "await rule cache debug output", self._ts.Disk.traffic_out.Name, "Reusing cached regular expressions from", 3
        )
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
            "$$reused_primary -eq 2 -a $$reused_secondary -eq 1"
        )
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts
        return tr


TestRegexRemapRuleCache(ts, ''.join(regex_remap_lines), curl_and_args)
