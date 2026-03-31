'''
Test the traffic_ctl config convert storage command.
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

Test.Summary = 'Test traffic_ctl config convert storage command.'

# Create an ATS process to get the environment with PATH set correctly.
ts = Test.MakeATSProcess("ts", enable_cache=False)

# Test 1: Basic conversion (spans only, no volume=N annotations).
tr = Test.AddTestRun("Test basic storage.config + volume.config conversion")
tr.Setup.Copy('legacy_config/basic.storage.config')
tr.Setup.Copy('legacy_config/basic.volume.config')
tr.Processes.Default.Command = \
    'traffic_ctl config convert storage basic.storage.config basic.volume.config -'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Test 2: Exclusive volume assignments (volume=N per span line).
tr = Test.AddTestRun("Test storage.config with exclusive volume=N span assignments")
tr.Setup.Copy('legacy_config/exclusive.storage.config')
tr.Setup.Copy('legacy_config/exclusive.volume.config')
tr.Processes.Default.Command = \
    'traffic_ctl config convert storage exclusive.storage.config exclusive.volume.config -'
tr.Processes.Default.Streams.stdout = "gold/exclusive.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Test 3: Spans only, no volume.config (missing volume.config is treated as empty).
tr = Test.AddTestRun("Test storage.config with no volume.config (spans only)")
tr.Setup.Copy('legacy_config/no_volumes.storage.config')
tr.Processes.Default.Command = \
    'traffic_ctl config convert storage no_volumes.storage.config /nonexistent/volume.config -'
tr.Processes.Default.Streams.stdout = "gold/no_volumes.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Test 4: Output to file.
tr = Test.AddTestRun("Test output to file")
tr.Setup.Copy('legacy_config/basic.storage.config')
tr.Setup.Copy('legacy_config/basic.volume.config')
tr.Processes.Default.Command = \
    'traffic_ctl config convert storage basic.storage.config basic.volume.config generated.yaml' \
    ' > /dev/null && cat generated.yaml'
tr.Processes.Default.Streams.stdout = "gold/basic.yaml"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
