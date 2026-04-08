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
Test traffic_ctl --error-level flag with severity-aware exit codes.

Annotations without explicit severity default to DIAG, so they exit 0 with the
default --error-level=error threshold.  Only annotations with severity >= the
threshold cause a non-zero exit.

Covers three categories:
  1. Handlers without explicit severity (annotations default to DIAG).
  2. Protocol-level errors (empty data) and record-level errors (separate code
     path) — these always exit 2 regardless of --error-level.
  3. Successful commands always exit 0 regardless of --error-level.
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")

# ===================================================================
# Category 1: Handler WITHOUT explicit severity (defaults to DIAG)
# ===================================================================

# 1. Drain the server — first time succeeds.
tr = Test.AddTestRun("drain server - first time succeeds")
tr.Processes.Default.Command = 'traffic_ctl server drain'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts
tr.DelayStart = 3

# 2. Drain again (default --error-level=error) — "Server already draining" has
#    no explicit severity, defaults to DIAG.  DIAG < ERROR → exit 0.
tr = Test.AddTestRun("drain again - default error-level, exit 0 for diag")
tr.Processes.Default.Command = 'traffic_ctl server drain'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Server already draining", "should report already draining")

# 3. Drain again with --error-level=diag — DIAG >= DIAG → exit 2.
tr = Test.AddTestRun("drain again - error-level=diag, exit 2 for diag")
tr.Processes.Default.Command = 'traffic_ctl --error-level=diag server drain'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

# 4. Drain again with --error-level=warn — DIAG < WARN → exit 0.
tr = Test.AddTestRun("drain again - error-level=warn, exit 0 for diag")
tr.Processes.Default.Command = 'traffic_ctl --error-level=warn server drain'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 5. Undo drain — first time succeeds.
tr = Test.AddTestRun("undo drain - first time succeeds")
tr.Processes.Default.Command = 'traffic_ctl server drain --undo'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 6. Undo drain again (default) — "Server is not draining" defaults to DIAG.
#    DIAG < ERROR → exit 0.
tr = Test.AddTestRun("undo drain again - default error-level, exit 0 for diag")
tr.Processes.Default.Command = 'traffic_ctl server drain --undo'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 7. Undo drain again with --error-level=diag — DIAG >= DIAG → exit 2.
tr = Test.AddTestRun("undo drain again - error-level=diag, exit 2 for diag")
tr.Processes.Default.Command = 'traffic_ctl --error-level=diag server drain --undo'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

# ===================================================================
# Category 2: Protocol/record errors (always exit 2)
# ===================================================================

# 8-9. Unknown RPC method — JSONRPC protocol error (METHOD_NOT_FOUND).
#      Protocol errors have no data annotations, so appExitCodeFromResponse
#      sees empty data → always exit 2 regardless of --error-level.
tr = Test.AddTestRun("unknown rpc method - default error-level, exit 2")
tr.Processes.Default.Command = 'traffic_ctl rpc invoke nonexistent_rpc_method'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

tr = Test.AddTestRun("unknown rpc method - error-level=fatal, still exit 2")
tr.Processes.Default.Command = 'traffic_ctl --error-level=fatal rpc invoke nonexistent_rpc_method'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

# 10-11. Bad config get — record-level errors go through a separate code
#        path (print_record_error_list) that hard-codes CTRL_EX_ERROR.
#        --error-level has no effect here.
tr = Test.AddTestRun("config get bad record - default error-level, exit 2")
tr.Processes.Default.Command = 'traffic_ctl config get nonexistent.record.name'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

tr = Test.AddTestRun("config get bad record - error-level=fatal, still exit 2")
tr.Processes.Default.Command = 'traffic_ctl --error-level=fatal config get nonexistent.record.name'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 2
tr.StillRunningAfter = ts

# ===================================================================
# Category 3: Successful commands — exit 0 regardless of --error-level
# ===================================================================

# 12. Successful config get — exit 0 with default level.
tr = Test.AddTestRun("config get valid record - exit 0")
tr.Processes.Default.Command = 'traffic_ctl config get proxy.config.http.server_ports'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 13. Successful config get with --error-level=diag — still exit 0.
tr = Test.AddTestRun("config get valid record - error-level=diag, exit 0")
tr.Processes.Default.Command = 'traffic_ctl --error-level=diag config get proxy.config.http.server_ports'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
