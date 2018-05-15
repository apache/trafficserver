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
import sys
import time

Test.Summary = '''
Test for verify of runroot from traffic_layout.
'''
Test.ContinueOnFail = True

p = Test.MakeATSProcess("ts")
ts_root = p.Env['TS_ROOT']

# create runroot
path = os.path.join(ts_root, "runroot")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = True

# verify test #1
tr = Test.AddTestRun("verify runroot test1")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout verify --path " + path
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    os.path.join(path, "bin"), "example bindir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    os.path.join(path, "var/log/trafficserver"), "example logdir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Read Permission: ", "read permission output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Execute Permission: ", "execute permission output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Write Permission: ", "write permission output")

# verify test #2
tr = Test.AddTestRun("verify runroot test2")
tr.Processes.Default.Command = "cd " + path + \
    ";" + "bin/traffic_layout verify --path " + path
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    os.path.join(path, "bin"), "example bindir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    os.path.join(path, "var/log/trafficserver"), "example logdir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Read Permission: ", "read permission output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Execute Permission: ", "execute permission output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Write Permission: ", "write permission output")
