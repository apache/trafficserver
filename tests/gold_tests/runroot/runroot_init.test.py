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
Test for init of runroot from traffic_layout.
'''
Test.ContinueOnFail = True

# init from pass in path
path1 = os.path.join(Test.RunDirectory, "runroot1")
tr = Test.AddTestRun("Test traffic_layout init #1")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path1
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path1, "runroot.yaml"))
f.Exists = True

# init to relative directory
path2 = os.path.join(Test.RunDirectory, "runroot2")
tr = Test.AddTestRun("Test traffic_layout init #2")
tr.Processes.Default.Command = "cd " + Test.RunDirectory + ";$ATS_BIN/traffic_layout init --path runroot2"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path2, "runroot.yaml"))
f.Exists = True

# init to cwd
path3 = os.path.join(Test.RunDirectory, "runroot3")
tr = Test.AddTestRun("Test traffic_layout init #3")
tr.Processes.Default.Command = "mkdir " + path3 + ";cd " + path3 + ";$ATS_BIN/traffic_layout init"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path3, "runroot.yaml"))
f.Exists = True

# --force init to an non-empty directory
path4 = os.path.join(Test.RunDirectory, "runroot4")
tr = Test.AddTestRun("Test traffic_layout init #4")
randomfile = os.path.join(path4, "foo")
tr.Processes.Default.Command = "mkdir " + path4 + ";touch " + randomfile + ";$ATS_BIN/traffic_layout init --force --path " + path4
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path4, "runroot.yaml"))
f.Exists = True
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Forcing creating runroot", "force message")

# create runroot with junk to guarantee only traffic server related files are copied
path5 = os.path.join(Test.RunDirectory, "runroot5")
junk1 = os.path.join(path1, "bin/junk1")
junk2 = os.path.join(path1, "lib/junk2")
junk3 = os.path.join(path1, "var/junk3")

tr = Test.AddTestRun("Test traffic_layout init #5")
# create the junk files in runroot1 and create(init and copy) runroot5 from runroot 1
tr.Processes.Default.Command = "touch " + junk1 + ";" + "touch " + junk2 + ";" + \
    "touch " + junk3 + ";" + path1 + "/bin/traffic_layout init --path " + path5
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File(os.path.join(path5, "runroot.yaml"))
f.Exists = True
# check if the junk file is created and not copied to the new runroot
f = tr.Disk.File(junk1)
f.Exists = True
f = tr.Disk.File(os.path.join(path5, "bin/junk1"))
f.Exists = False
f = tr.Disk.File(junk2)
f.Exists = True
f = tr.Disk.File(os.path.join(path5, "lib/junk2"))
f.Exists = False
f = tr.Disk.File(junk3)
f.Exists = True
f = tr.Disk.File(os.path.join(path5, "var/junk3"))
f.Exists = False
