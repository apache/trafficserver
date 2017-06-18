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

Test.Summary = '''
Test that Trafficserver starts with default configurations.
'''

Test.SkipUnless(Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"))

p = Test.MakeATSProcess("ts", command="traffic_cop --debug --stdout", select_ports=False)
t = Test.AddTestRun("Test traffic server started properly")
t.StillRunningAfter = Test.Processes.ts

p = t.Processes.Default
p.Command = "curl http://127.0.0.1:8080"
p.ReturnCode = 0
p.StartBefore(Test.Processes.ts, ready=When.PortOpen(8080))
