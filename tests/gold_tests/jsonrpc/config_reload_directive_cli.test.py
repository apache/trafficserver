'''
Verify traffic_ctl command line parsing for the reload options that take a
variable number of values, --directive (-D) and --data (-d).

Options declared with MORE_THAN_ZERO_ARG_N used to consume every remaining
token, so any option written after -D was silently swallowed as a directive
value and never parsed.  -D therefore had to be the last option, and -D could
not be combined with -d.  These runs assert on the JSONRPC request that
traffic_ctl builds (printed by -f rpc), because the subject under test is the
command line parsing rather than the server side handling of the reload.
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
