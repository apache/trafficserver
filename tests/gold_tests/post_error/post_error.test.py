'''
Test post_err
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
import sys
import os
Test.Summary = '''
Test post_error
'''

# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)
Test.ContinueOnFail = True

# build post server code
tr=Test.Build(target='post_server',sources=['post_server.c'])
tr.Setup.Copy('post_server.c')

# This sets up a reasonable fallback in the event the absolute path to this interpreter cannot be determined
executable = sys.executable if sys.executable else 'python3'

tr = Test.AddTestRun()
tr.Setup.Copy('create_post_body.py')
tr.Processes.Default.Command = "%s create_post_body.py" % executable

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)

# create Process
server = Test.Processes.Process("post_server")
port = 8888
command = "./post_server {0}".format(port)

# create process
server.Command = command
server.Ready = When.PortOpen(port)
server.ReturnCode = Any(None, 0)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(port)
)

ts.Disk.records_config.update({
    # enable ssl port
    'proxy.config.http.server_ports': '{0}'.format(ts.Variables.port)
})



tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -H"Expect:" -v -k -d "@postbody" http://127.0.0.1:{0}/'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/post_error.gold"
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = server

