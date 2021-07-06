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


import os

Test.Summary = '''
Test command: traffic_ctl config describe proxy.config.http.background_fill_completed_threshold (YTSATS-3309)
'''
Test.testName = 'Float in conf_remap Config Test'

ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True)

# Add dummy remap rule
ts.Disk.remap_config.AddLine(
    'map http://cdn.example.com/ http://origin.example.com/ @plugin=conf_remap.so @pparam={file}'.format(
        file=os.path.join(ts.RunDirectory, 'ts/config/delain.config'))
)

ts.Disk.delain_config.AddLine(
    'CONFIG proxy.config.http.background_fill_completed_threshold FLOAT 0.500000'
)

#
# Test body
#

# First reload
tr = Test.AddTestRun("traffic_ctl command")
tr.Env = ts.Env
tr.TimeOut = 5
tr.StillRunningAfter = ts

p = tr.Processes.Default
p.Command = "traffic_ctl config describe proxy.config.http.background_fill_completed_threshold"
p.ReturnCode = 0
p.StartBefore(Test.Processes.ts, ready=When.FileExists(os.path.join(tr.RunDirectory, 'ts/log/diags.log')))
