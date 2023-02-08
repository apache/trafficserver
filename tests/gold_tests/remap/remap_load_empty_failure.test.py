'''
Verify correct behavior of regex_map in remap.config.
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
Test minimum rules on load - fail on missing file.
'''
ts = Test.MakeATSProcess("ts")
ts.Disk.remap_config.AddLine(f"")  # empty file
ts.Disk.records_config.update({'proxy.config.url_remap.min_rules_required': 1})
ts.ReturnCode = 33  # expect to Emergency fail due to empty "remap.config".
ts.Ready = 0

tr = Test.AddTestRun("test")

# We have to wait upon TS to emit the expected log message, but it cannot be
# the ts Ready criteria because autest might detect the process going away
# before it detects the log message. So we add a separate process that waits
# upon the log message.
watcher = Test.Processes.Process("watcher")
watcher.Command = "sleep 1"
watcher.Ready = When.FileContains(ts.Disk.diags_log.Name, "remap.config failed to load")
watcher.StartBefore(ts)

tr.Processes.Default.Command = "echo howdy"
tr.TimeOut = 5
tr.Processes.Default.StartBefore(watcher)
