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
Test for verify of runroot from traffic_layout.
'''

prefix = Test.Variables["PREFIX"]
bindir = Test.Variables["BINDIR"]
logdir = Test.Variables["LOGDIR"]

if bindir.startswith(prefix):
    # get the bin directory based on removing the common prefix
    binsuffix = bindir[len(prefix) + 1:]
else:
    # given a custom setup this might work.. or it might not
    binsuffix = bindir

if logdir.startswith(prefix):
    # get the bin directory based on removing the common prefix
    logsuffix = bindir[len(prefix) + 1:]
else:
    # given a custom setup this might work.. or it might not
    logsuffix = logdir


# create runroot
path = os.path.join(Test.RunDirectory, "runroot")
tr = Test.AddTestRun("Create runroot")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
f = tr.Disk.File(os.path.join(path, "runroot.yaml"))
f.Exists = True

# verify test #1
tr = Test.AddTestRun("verify runroot test1")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout verify --path " + path

tr.Processes.Default.Streams.All = Testers.ContainsExpression(os.path.join(path, binsuffix), "example bindir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(os.path.join(path, logsuffix), "example logdir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PASSED", "contain passed message")

# verify test #2
bin_path = Test.Variables.BINDIR[Test.Variables.BINDIR.find(Test.Variables.PREFIX) + len(Test.Variables.PREFIX) + 1:]
tr = Test.AddTestRun("verify runroot test2")
tr.Processes.Default.Command = "cd " + path + ";" + os.path.join(bin_path, "traffic_layout") + " verify --path " + path

tr.Processes.Default.Streams.All = Testers.ContainsExpression(os.path.join(path, binsuffix), "example bindir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression(os.path.join(path, logsuffix), "example logdir output")
tr.Processes.Default.Streams.All = Testers.ContainsExpression("PASSED", "contain passed message")
