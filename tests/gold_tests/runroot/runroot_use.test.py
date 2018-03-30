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
Test for using of runroot from traffic_layout.
'''
Test.ContinueOnFail = True

p = Test.MakeATSProcess("ts")
ts_root = p.Env['TS_ROOT']

# create two runroot for testing
path = os.path.join(ts_root, "runroot")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = True

path2 = os.path.join(ts_root, "runroot2")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path2
f = tr.Disk.File(os.path.join(path2, "runroot_path.yml"))
f.Exists = True

# 1. --run-root use path cmd
tr = Test.AddTestRun("use runroot via commandline")
tr.Processes.Default.Command = os.path.join("$ATS_BIN/traffic_layout info --run-root=" + path)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PREFIX: " + path, "commandline runroot path")

# 2. use cwd as runroot
tr = Test.AddTestRun("use runroot via cwd")
tr.Processes.Default.Command = "cd " + path + ";" + os.path.join("$ATS_BIN/traffic_layout info")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PREFIX: " + path, "cwd runroot path")

# 4. use path directly bin
tr = Test.AddTestRun("use runroot via bin executable")
tr.Processes.Default.Command = os.path.join(path, "bin/traffic_layout info")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PREFIX: " + path, "bin path")

# 3. TS_RUNROOT ENV variable
tr = Test.AddTestRun("use runroot via TS_RUNROOT")
tr.Processes.Default.Env["TS_RUNROOT"] = path2
tr.Processes.Default.Command = os.path.join("$ATS_BIN/traffic_layout info")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PREFIX: " + path2, "$TS_RUNROOT Env path")
