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
Test that use for runroot from traffic_layout is all functional.
'''
Test.ContinueOnFail = True

p = Test.MakeATSProcess("ts")
path = os.path.join(p.Env['TS_ROOT'], "runroot")

# normal init from pass in path
tr = Test.AddTestRun("Test traffic_layout init")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
tr.Processes.Default.ReturnCode = 0
d = tr.Disk.Directory(path)
d.Exists = True
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = True

# remove from pass in path
tr = Test.AddTestRun("Test traffoc_layout remove")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout remove --path " + path
tr.Processes.Default.ReturnCode = 0
d = tr.Disk.Directory(path)
d.Exists = False
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = False

# path += '/'

# #use env variable to init
# tr = Test.AddTestRun("Test traffic_layout ENV init")
# tr.Processes.Default.Env["TS_RUNROOT"] = path
# tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init"
# tr.Processes.Default.ReturnCode = 0
# d = tr.Disk.Directory(path)
# d.Exists = True
# f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
# f.Exists = True

# #use env variable to remove
# tr = Test.AddTestRun("Test traffic_layout ENV remove")
# tr.Processes.Default.Env["TS_RUNROOT"] = path
# tr.Processes.Default.Command = "$ATS_BIN/traffic_layout remove"
# tr.Processes.Default.ReturnCode = 0
# d = tr.Disk.Directory(path)
# d.Exists = False
# f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
# f.Exists = False
