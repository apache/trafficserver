'''
Test the traffic_ctl config convert ssl_multicert command.
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

Test.Summary = 'Test traffic_ctl config convert ssl_multicert command.'

# Create an ATS process to get the environment with PATH set correctly.
ts = Test.MakeATSProcess("ts", enable_cache=False)

# Test 1: Basic config conversion.
tr = Test.AddTestRun("Test basic ssl_multicert.config conversion")
tr.Setup.Copy('legacy_config/basic.config')
tr.Processes.Default.Command = 'traffic_ctl config convert ssl_multicert basic.config -'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Test 2: Full config with all options.
tr = Test.AddTestRun("Test full ssl_multicert.config conversion with all options")
tr.Setup.Copy('legacy_config/full.config')
tr.Processes.Default.Command = 'traffic_ctl config convert ssl_multicert full.config -'
tr.Processes.Default.Streams.stdout = "gold/full.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Test 3: Config with quoted values.
tr = Test.AddTestRun("Test ssl_multicert.config with quoted values")
tr.Setup.Copy('legacy_config/quoted.config')
tr.Processes.Default.Command = 'traffic_ctl config convert ssl_multicert quoted.config -'
tr.Processes.Default.Streams.stdout = "gold/quoted.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Test 4: Output to file.
tr = Test.AddTestRun("Test output to file")
tr.Setup.Copy('legacy_config/basic.config')
tr.Processes.Default.Command = 'traffic_ctl config convert ssl_multicert basic.config generated.yaml > /dev/null && cat generated.yaml'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
