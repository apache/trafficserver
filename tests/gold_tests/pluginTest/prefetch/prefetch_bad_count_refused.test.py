'''
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
Test that prefetch.so treats a --fetch-count that does not fit in an unsigned as a configuration
error: ATS refuses to load the remap rule (and fails to start) instead of truncating the value.

The value below is a string of decimal digits, so it passes a digits-only check, but it exceeds the
range of the unsigned the plugin stores it in.
'''

ts = Test.MakeATSProcess("ts")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'prefetch',
})
ts.Disk.remap_config.AddLine(
    "map http://domain.in http://127.0.0.1:8080" + " @plugin=prefetch.so" + " @pparam=--front=true" +
    " @pparam=--fetch-policy=simple" + " @pparam=--fetch-count=5000000000")

ts.ReturnCode = 33  # Emergency exit: remap.config failed to load.
ts.Ready = 0
# ATS is expected to log the rejection; this ContainsExpression both asserts it and replaces the
# default "diags.log must not contain ERROR:" check (the rejection is logged via TSError).
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "invalid --fetch-count '5000000000'", "an out-of-range fetch-count must be rejected at config load")

tr = Test.AddTestRun("prefetch rejects an out-of-range fetch-count at load")
# Wait for the rejection message with a separate watcher: gating ts readiness on the log line directly
# can race the process exiting before autest observes the line.
watcher = Test.Processes.Process("watcher")
watcher.Command = "sleep 30"
watcher.Ready = When.FileContains(ts.Disk.diags_log.Name, "invalid --fetch-count '5000000000'")
watcher.StartBefore(ts)

tr.Processes.Default.Command = "echo done"
tr.TimeOut = 30
tr.Processes.Default.StartBefore(watcher)
