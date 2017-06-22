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
Test a basic of UDPNet
'''
Test.testName = ""
# need netcat
Test.SkipUnless(
    Condition.HasProgram("nc", "netcat need to be installed on system for this test to work")
)
Test.ContinueOnFail = True

# Define udp_echo_server
ts = Test.MakeUDPEchoServerProcess("ts")

# call localhost straight
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'sleep 1 && echo "hello" | nc -u -w 1 127.0.0.1 4443'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "gold/udp_echo.gold"
