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
Test for remove of runroot from traffic_layout.
'''
Test.ContinueOnFail = True

# create three runroot for removing testing
path1 = os.path.join(Test.RunDirectory, "runroot1")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path1
f = tr.Disk.File(os.path.join(path1, "runroot.yaml"))
f.Exists = True

path2 = os.path.join(Test.RunDirectory, "runroot2")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path2
f = tr.Disk.File(os.path.join(path2, "runroot.yaml"))
f.Exists = True

path3 = os.path.join(Test.RunDirectory, "runroot3")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path3
f = tr.Disk.File(os.path.join(path3, "runroot.yaml"))
f.Exists = True

# normal remove from pass in path
tr = Test.AddTestRun("Test traffic_layout remove #1")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout remove --path " + path1
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path1, "runroot.yaml"))
f.Exists = False
d = tr.Disk.Directory(path1)
d.Exists = False

# remove of relative path
tr = Test.AddTestRun("Test traffic_layout remove #2")
tr.Processes.Default.Command = "cd " + Test.RunDirectory + ";$ATS_BIN/traffic_layout remove --path runroot2"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path2, "runroot.yaml"))
f.Exists = False
d = tr.Disk.Directory(path2)
d.Exists = False

# remove cwd
tr = Test.AddTestRun("Test traffic_layout remove #3")
tr.Processes.Default.Command = "mkdir " + path3 + ";cd " + path3 + ";$ATS_BIN/traffic_layout remove"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path3, "runroot.yaml"))
f.Exists = False
d = tr.Disk.Directory(path3)
d.Exists = True
