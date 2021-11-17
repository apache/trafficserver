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
Test for using of runroot of traffic_manager.
'''
Test.ContinueOnFail = False

# create runroot for testing
runroot_path = os.path.join(Test.RunDirectory, "runroot")
rr_file = os.path.join(Test.RunDirectory, "rr_tmp")

tr = Test.AddTestRun("create runroot and deal with it")
tr.Processes.Default.Command = "$ATS_BIN/traffic_layout init --path " + runroot_path + " --absolute; " + \
    "mkdir " + rr_file + "; mv " + \
    os.path.join(runroot_path, "runroot.yaml") + " " + \
    os.path.join(rr_file, "runroot.yaml")
f = tr.Disk.File(os.path.join(rr_file, "runroot.yaml"))
f.Exists = True


def StopProcess(event, time):
    if event.TotalRunTime > time:
        event.object.Stop()
    return 0, "stop manager process", "manager will be killed"


tr = Test.AddTestRun("manager runroot test")

trafficserver_dir = os.path.join(runroot_path, 'var', 'trafficserver')
tr.ChownForATSProcess(trafficserver_dir)

p = tr.Processes.Default
p.Command = "$ATS_BIN/traffic_manager --run-root=" + rr_file
p.RunningEvent.Connect(Testers.Lambda(lambda ev: StopProcess(ev, 10)))
p.Streams.All = Testers.ContainsExpression("traffic_server: using root directory '" +
                                           runroot_path + "'", "check if the right runroot is passed down")
