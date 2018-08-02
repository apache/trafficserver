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
Test for expected error and failure of runroot from traffic_layout.
'''
Test.ContinueOnFail = True

# create runroot
path = os.path.join(Test.RunDirectory, "runroot")
tr = Test.AddTestRun()
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = True

# bad command line args
tr = Test.AddTestRun("wrong usage")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path"
tr.Processes.Default.Streams.All = Testers.ContainsExpression("init Usage", "init incorrect usage")
tr.Processes.Default.Streams.All = Testers.ContainsExpression("remove Usage", "init incorrect usage")
tr.Processes.Default.Streams.All = Testers.ContainsExpression("verify Usage", "init incorrect usage")

# use existing runroot
tr = Test.AddTestRun("using existing runroot")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Using existing runroot", "init incorrect usage")
f = tr.Disk.File(os.path.join(path, "runroot_path.yml"))
f.Exists = True

# create runroot inside another
path_inside = os.path.join(path, "runroot")
tr = Test.AddTestRun("create runroot inside runroot")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + path_inside
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Cannot create runroot inside another runroot", "init incorrect usage")
f = tr.Disk.File(os.path.join(path_inside, "runroot_path.yml"))
f.Exists = False

# remove invalid runroot
path_invalid = os.path.join(Test.RunDirectory, "tmp")
tr = Test.AddTestRun("remove invalid runroot")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout remove --path " + path_invalid
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Unable to read", "remove incorrect usage")

# verify invalid runroot
tr = Test.AddTestRun("verify invalid runroot")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout verify --path " + path_invalid
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Unable to read", "verify incorrect usage")
