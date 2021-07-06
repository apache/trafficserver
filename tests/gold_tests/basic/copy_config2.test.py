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

Test.Summary = "Test start up of Traffic server with generated ports of more than one servers at the same time"

# set up some ATS processes
ts1 = Test.MakeATSProcess("ts1")
ts2 = Test.MakeATSProcess("ts2")

# setup a testrun
t = Test.AddTestRun("Talk to ts1")
t.StillRunningAfter = ts1
t.StillRunningAfter += ts2
p = t.Processes.Default
p.Command = "curl 127.0.0.1:{0}".format(ts1.Variables.port)
p.ReturnCode = 0
p.StartBefore(Test.Processes.ts1)
p.StartBefore(Test.Processes.ts2)

# setup a testrun
t = Test.AddTestRun("Talk to ts2")
t.StillRunningBefore = ts1
t.StillRunningBefore += ts2
t.StillRunningAfter = ts1
t.StillRunningAfter += ts2
p = t.Processes.Default
p.Command = "curl 127.0.0.1:{0}".format(ts2.Variables.port)
p.ReturnCode = 0
