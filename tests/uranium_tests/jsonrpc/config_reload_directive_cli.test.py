'''
Verify traffic_ctl command line parsing for the reload options that take a
variable number of values, --directive (-D) and --data (-d).

Options declared with MORE_THAN_ZERO_ARG_N used to consume every remaining
token, so any option written after -D was silently swallowed as a directive
value and never parsed.  -D therefore had to be the last option, and -D could
not be combined with -d.  Once collection stops at the following option, the
option can be written more than once, and each occurrence has to keep the
values it collected rather than replace the ones before it.  These runs assert
on the JSONRPC request that traffic_ctl builds (printed by -f rpc), because the
subject under test is the command line parsing rather than the server side
handling of the reload.
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

Test.Summary = 'Verify traffic_ctl -D/-d argument parsing for config reload'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
ts.StartupTimeout = 30

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|config.reload',
})

ts.Disk.ip_allow_yaml.AddLines([
    'ip_allow:',
    '- apply: in',
    '  ip_addrs: 0/0',
    '  action: allow',
    '  methods: ALL',
])

# ============================================================================
# Test 1: an option written after -D keeps its own argument
# ============================================================================
tr = Test.AddTestRun("Option after -D is not consumed as a directive value")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "traffic_ctl config reload -D ip_allow.id=foo -t cli_token_1 -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"token": "cli_token_1"', "-t must survive after -D")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"id": "foo"', "the directive must still be parsed")
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: several directives, then an option
# ============================================================================
tr = Test.AddTestRun("Multiple directives followed by an option")
tr.Processes.Default.Command = "traffic_ctl config reload -D ip_allow.id=1 sni.id=2 -t cli_token_2 -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"token": "cli_token_2"', "-t must survive after -D")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"ip_allow"', "first directive key must be present")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"sni"', "second directive key must be present")
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: -D combined with -d, which the parser previously made impossible
# ============================================================================
tr = Test.AddTestRun("-D can be combined with -d")
tr.Processes.Default.Command = "traffic_ctl config reload -D ip_allow.id=foo -d 'ip_allow: {rules: [x]}' -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"rules"', "inline content from -d must be present")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"_reload"', "directives from -D must be present")
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: --directive=value keeps a value that itself contains '='
# ============================================================================
tr = Test.AddTestRun("--directive=value preserves embedded equal signs")
tr.Processes.Default.Command = "traffic_ctl config reload --directive=ip_allow.id=foo -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"id": "foo"', "the whole value must reach the request")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("Invalid directive format", "the value must parse cleanly")
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: "--" ends option recognition, so the value is taken literally and
# then rejected by the directive format check
# ============================================================================
tr = Test.AddTestRun("A value after -- is taken literally")
tr.Processes.Default.Command = "traffic_ctl config reload -D -- -m"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "Invalid directive format '-m'", "-m must be treated as a directive value, not as --monitor")
tr.StillRunningAfter = ts

# ============================================================================
# Test 6: -D without any directive would silently reload every handler
# ============================================================================
tr = Test.AddTestRun("-D requires at least one directive")
tr.Processes.Default.Command = "traffic_ctl config reload -D"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("requires at least one", "-D must not be a silent no-op")
tr.StillRunningAfter = ts

# ============================================================================
# Test 7: same for -d, where a silent full reload is especially misleading
# ============================================================================
tr = Test.AddTestRun("-d requires content")
tr.Processes.Default.Command = "traffic_ctl config reload -d"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("requires content", "-d must not be a silent no-op")
tr.StillRunningAfter = ts

# ============================================================================
# Test 8: a repeated -D keeps the directives of every occurrence
# ============================================================================
tr = Test.AddTestRun("A repeated -D accumulates its directives")
tr.Processes.Default.Command = "traffic_ctl config reload -D ip_allow.id=1 -D sni.id=2 -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"ip_allow"', "the first occurrence must survive the second")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"sni"', "the second occurrence must be present")
tr.StillRunningAfter = ts

# ============================================================================
# Test 9: repeating the option is how a directive is written after another
# option, since collection stops at the option rather than at the value
# ============================================================================
tr = Test.AddTestRun("A repeated -D survives an option written between the two")
tr.Processes.Default.Command = "traffic_ctl config reload -D ip_allow.id=1 -t cli_token_8 -D sni.id=2 -f rpc"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"token": "cli_token_8"', "-t must keep its own value")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"ip_allow"', "the directive before -t must survive")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"sni"', "the directive after -t must be parsed")
tr.StillRunningAfter = ts

# ============================================================================
# Test 10: the documented multi source reload, where dropping one -d would
# leave its handler out of the reload without reporting anything
# ============================================================================
tr = Test.AddTestRun("A repeated -d merges every source")
tr.Processes.Default.Command = ("traffic_ctl config reload -d 'ip_allow: {rules: [x]}' -d 'sni: {rules: [y]}' -f rpc")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"ip_allow"', "content from the first -d must be present")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('"sni"', "content from the second -d must be present")
tr.StillRunningAfter = ts

# ============================================================================
# Test 11: an empty -d token is no content. The token survives the argument
# count, so without the content check the request degrades to a full reload
# ============================================================================
tr = Test.AddTestRun("-d with an empty argument is reported as empty")
tr.Processes.Default.Command = "traffic_ctl config reload -d ''"
# autest's shell detection indexes arg[0], so an empty argument needs the shell path.
tr.Processes.Default.ForceUseShell = True
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "received an empty value", "an empty -d must be reported as empty, not as a missing argument")
tr.StillRunningAfter = ts

# ============================================================================
# Test 12: same for -D
# ============================================================================
tr = Test.AddTestRun("-D with an empty argument is reported as empty")
tr.Processes.Default.Command = "traffic_ctl config reload -D ''"
tr.Processes.Default.ForceUseShell = True
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "received an empty value", "an empty -D must be reported as empty, not as a missing argument")
tr.StillRunningAfter = ts

# ============================================================================
# Test 13: an empty token is refused even next to a real one. This is what an
# unset variable in a script written with several -d looks like, and accepting
# it would reload fewer configs than were asked for without saying so
# ============================================================================
tr = Test.AddTestRun("An empty -d token is refused next to a real one")
tr.Processes.Default.Command = ("traffic_ctl config reload -d '' -d 'ip_allow: {rules: [x]}'")
tr.Processes.Default.ForceUseShell = True
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "received an empty value", "a partial reload must not happen silently")
tr.StillRunningAfter = ts
