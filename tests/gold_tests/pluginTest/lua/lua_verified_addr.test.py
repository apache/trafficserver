'''
Test lua verified address functionality
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
Test lua verified address get/set functionality
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

# Define ATS process
ts = Test.MakeATSProcess("ts")

# Configure remap
ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1 @plugin=tslua.so @pparam=verified_addr.lua")

# Copy the Lua script
ts.Setup.Copy("verified_addr.lua", ts.Variables.CONFIGDIR)

# Enable debug logging
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ts_lua'
})

# Test 1: IPv4 verified address
tr = Test.AddTestRun("Lua Verified Address - IPv4")
ps = tr.Processes.Default
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(
    f"-s -H 'X-Real-IP: 192.0.2.100' http://127.0.0.1:{ts.Variables.port}",
    ts=ts
)
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "initial:nil;set:success;get:192.0.2.100:2;",
    "IPv4 verified address should be set and retrieved correctly"
)
tr.StillRunningAfter = ts

# Test 2: IPv6 verified address
tr = Test.AddTestRun("Lua Verified Address - IPv6")
ps = tr.Processes.Default
tr.MakeCurlCommand(
    f"-s -H 'X-Real-IP-V6: 2001:db8::1' http://127.0.0.1:{ts.Variables.port}",
    ts=ts
)
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "initial:nil;setv6:success;getv6:2001:db8::1:10;",
    "IPv6 verified address should be set and retrieved correctly"
)
tr.StillRunningAfter = ts

# Test 3: Invalid IP address (should be rejected)
tr = Test.AddTestRun("Lua Verified Address - Invalid IP")
ps = tr.Processes.Default
tr.MakeCurlCommand(
    f"-s -H 'X-Invalid-IP: not.a.valid.ip' http://127.0.0.1:{ts.Variables.port}",
    ts=ts
)
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "invalid:rejected;",
    "Invalid IP address should be rejected"
)
tr.StillRunningAfter = ts

# Test 4: Both IPv4 and IPv6 in sequence
tr = Test.AddTestRun("Lua Verified Address - IPv4 then IPv6")
ps = tr.Processes.Default
tr.MakeCurlCommand(
    f"-s -H 'X-Real-IP: 203.0.113.42' -H 'X-Real-IP-V6: 2001:db8::42' http://127.0.0.1:{ts.Variables.port}",
    ts=ts
)
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "initial:nil;set:success;get:203.0.113.42:2;setv6:success;getv6:2001:db8::42:10;",
    "Both IPv4 and IPv6 verified addresses should work in sequence"
)
tr.StillRunningAfter = ts
