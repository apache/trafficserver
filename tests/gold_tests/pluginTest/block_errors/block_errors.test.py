"""
Verify block_errors plugin message handling.
"""
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
Verify block_errors plugin message handling via traffic_ctl.
'''

Test.SkipUnless(Condition.PluginExists('block_errors.so'),)

# Define ATS and configure it.
ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'block_errors',
})

# Configure block_errors plugin with initial values.
ts.Disk.plugin_config.AddLine('block_errors.so')

# Verify the plugin loads.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "loading plugin.*block_errors.so", "Verify the block_errors plugin got loaded.")

#
# Test 1: Verify the plugin starts with default values.
#
tr = Test.AddTestRun("Verify plugin starts with default values.")
tr.Processes.Default.Command = "echo verifying plugin starts with default values"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Verify the default values are logged at startup.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    "reset limit: 1000 per minute, timeout limit: 4 minutes, shutdown connection: 0 enabled: 1",
    "Verify block_errors starts with default values.")

#
# Test 2: Verify changing the 'enabled' setting via traffic_ctl.
#
tr = Test.AddTestRun("Verify changing 'enabled' via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg block_errors.enabled 0"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the enabled change.")
tr.Processes.Default.Command = "echo awaiting enabled change"
tr.Processes.Default.ReturnCode = 0
await_enabled = tr.Processes.Process('await_enabled', 'sleep 30')
await_enabled.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: command=enabled data=0")
tr.Processes.Default.StartBefore(await_enabled)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: command=enabled data=0", "Verify block_errors received the enabled command.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "reset limit: 1000 per minute, timeout limit: 4 minutes, shutdown connection: 0 enabled: 0",
    "Verify block_errors applied the enabled=0 setting.")

#
# Test 3: Verify changing the 'limit' setting via traffic_ctl.
#
tr = Test.AddTestRun("Verify changing 'limit' via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg block_errors.limit 500"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the limit change.")
tr.Processes.Default.Command = "echo awaiting limit change"
tr.Processes.Default.ReturnCode = 0
await_limit = tr.Processes.Process('await_limit', 'sleep 30')
await_limit.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: command=limit data=500")
tr.Processes.Default.StartBefore(await_limit)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: command=limit data=500", "Verify block_errors received the limit command.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "reset limit: 500 per minute", "Verify block_errors applied the limit=500 setting.")

#
# Test 4: Verify changing the 'cycles' setting via traffic_ctl.
#
tr = Test.AddTestRun("Verify changing 'cycles' via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg block_errors.cycles 8"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the cycles change.")
tr.Processes.Default.Command = "echo awaiting cycles change"
tr.Processes.Default.ReturnCode = 0
await_cycles = tr.Processes.Process('await_cycles', 'sleep 30')
await_cycles.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: command=cycles data=8")
tr.Processes.Default.StartBefore(await_cycles)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: command=cycles data=8", "Verify block_errors received the cycles command.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "timeout limit: 8 minutes", "Verify block_errors applied the cycles=8 setting.")

#
# Test 5: Verify changing the 'shutdown' setting via traffic_ctl.
#
tr = Test.AddTestRun("Verify changing 'shutdown' via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg block_errors.shutdown 1"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the shutdown change.")
tr.Processes.Default.Command = "echo awaiting shutdown change"
tr.Processes.Default.ReturnCode = 0
await_shutdown = tr.Processes.Process('await_shutdown', 'sleep 30')
await_shutdown.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: command=shutdown data=1")
tr.Processes.Default.StartBefore(await_shutdown)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: command=shutdown data=1", "Verify block_errors received the shutdown command.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "shutdown connection: 1", "Verify block_errors applied the shutdown=1 setting.")

#
# Test 6: Verify an unknown command is handled gracefully.
#
tr = Test.AddTestRun("Verify unknown command is handled gracefully.")
tr.Processes.Default.Command = "traffic_ctl plugin msg block_errors.unknown_command test"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the unknown command response.")
tr.Processes.Default.Command = "echo awaiting unknown command response"
tr.Processes.Default.ReturnCode = 0
await_unknown = tr.Processes.Process('await_unknown', 'sleep 30')
await_unknown.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: unknown command 'unknown_command'")
tr.Processes.Default.StartBefore(await_unknown)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: unknown command 'unknown_command'", "Verify block_errors logs unknown commands.")

#
# Test 7: Verify messages for other plugins are ignored.
#
tr = Test.AddTestRun("Verify messages for other plugins are ignored.")
tr.Processes.Default.Command = "traffic_ctl plugin msg other_plugin.command test"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Await the other plugin message response.")
tr.Processes.Default.Command = "echo awaiting other plugin message response"
tr.Processes.Default.ReturnCode = 0
await_other = tr.Processes.Process('await_other', 'sleep 30')
await_other.Ready = When.FileContains(ts.Disk.traffic_out.Name, "msg_hook: message for a different plugin: other_plugin")
tr.Processes.Default.StartBefore(await_other)

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "msg_hook: message for a different plugin: other_plugin", "Verify block_errors ignores messages for other plugins.")
