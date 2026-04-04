'''
Test the traffic_ctl config convert remap command.
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

Test.Summary = 'Test traffic_ctl config convert remap command.'

ts = Test.MakeATSProcess("ts", enable_cache=False)

tr = Test.AddTestRun("Test legacy remap.config conversion to stdout")
tr.Setup.Copy('legacy_config/basic.config')
tr.Processes.Default.Command = 'traffic_ctl config convert remap basic.config -'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Test legacy remap.config conversion to file")
tr.Setup.Copy('legacy_config/basic.config')
tr.Processes.Default.Command = 'traffic_ctl config convert remap basic.config generated.yaml > /dev/null && cat generated.yaml'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
