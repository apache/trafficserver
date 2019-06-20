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

Test.Summary = "Test start up of Traffic server with configuration modification of starting port"

ts = Test.MakeATSProcess("ts", select_ports=False)
ts.Setup.ts.CopyConfig('config/records_8090.config', "records.config")
ts.Variables.port = 8090
ts.Ready = When.PortOpen(ts.Variables.port)
t = Test.AddTestRun("Test traffic server started properly")
t.Processes.Default.StartBefore(ts)
t.Command = "curl 127.0.0.1:{port}".format(port=ts.Variables.port)
t.ReturnCode = 0
t.StillRunningAfter = ts

