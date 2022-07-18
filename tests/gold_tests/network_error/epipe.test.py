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

from ports import get_port
import sys

Test.Summary = '''
Test that Trafficserver starts with default configurations.
'''

epiper = Test.Processes.Process("epiper")
epiper.Setup.CopyAs('epiper.py', Test.RunDirectory)
origin_port = get_port(epiper, "Port")
epiper.Command = f"{sys.executable} epiper.py {origin_port}"
epiper.Ready = When.PortOpen(origin_port)

ats = Test.MakeATSProcess("ats", select_ports=True)
ats.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{origin_port}'
)
ats.Ready = When.PortOpen(ats.Variables.port)

t = Test.AddTestRun("Verify behavior when remote server closes a connection.")
t.StillRunningAfter = ats

curl = t.Processes.Default
curl.Command = "curl http://127.0.0.1:{0}".format(ats.Variables.port)
curl.ReturnCode = 0
curl.StartBefore(ats)
curl.StartBefore(epiper)
