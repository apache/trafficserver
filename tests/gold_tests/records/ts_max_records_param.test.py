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

from jsonrpc import Notification, Request, Response

Test.Summary = 'Basic test for traffic_server --maxRecords behavior when setting different values.'

maxRecords = 1000

# 0  - maxRecords below the default value(2048). Traffic server should warn about this and use the default value.
ts = Test.MakeATSProcess(f"ts{maxRecords}", command=f"traffic_server --maxRecords {maxRecords}")
tr = Test.AddTestRun(f"--maxRecords {maxRecords}")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts
ts.Streams.All = Testers.ContainsExpression(
    f"Passed maxRecords value={maxRecords} is lower than the default value 2048. Default will be used.",
    "It should use the default value")

# 1  - maxRecords with just invalid number. Traffic server should warn about this and use the default value.
maxRecords = "abc"
ts = Test.MakeATSProcess(f"ts{maxRecords}", command=f"traffic_server --maxRecords {maxRecords}")
tr = Test.AddTestRun(f"--maxRecords {maxRecords}")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts
ts.Streams.All = Testers.ContainsExpression(
    f"Invalid 0 value for maxRecords. Default  2048 will be used.", "It should use the default value")

# 2  - maxRecords over the default value
maxRecords = 5000
ts = Test.MakeATSProcess(f"ts{maxRecords}", command=f"traffic_server --maxRecords {maxRecords}")
tr = Test.AddTestRun(f"--maxRecords {maxRecords}")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts
# At least it should not crash.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(f"NOTE: records parsing completed", "should all be good")
