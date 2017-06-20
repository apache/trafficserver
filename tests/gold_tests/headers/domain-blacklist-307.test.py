'''
Tests 307 responses are returned for matching domains
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
Tests 307 responses are returned for matching domains
'''

Test.SkipUnless(Condition.HasProgram("grep","grep needs to be installed on system for this test to work"))

ts=Test.MakeATSProcess("ts")
server=Test.MakeOriginServer("server")

REDIRECT_HOST='www.redirect307.test'
PASSTHRU_HOST='www.passthrough.test'

ts.Disk.remap_config.AddLine("""\
regex_map http://{0}/ http://{0}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules.conf
""".format(REDIRECT_HOST)
)

ts.Disk.header_rewrite_rules_conf.AddLine("""\
cond %{SEND_RESPONSE_HDR_HOOK}
set-header Location "%<cque>"
set-status 307
""")

ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'remap,header_rewrite,dbg_header_rewrite,header_rewrite_dbg',
    })

Test.Setup.Copy(os.path.join(os.pardir,os.pardir,'tools','tcp_client.py'))
Test.Setup.Copy('data')

redirect307tr=Test.AddTestRun("Test domain {0}".format(REDIRECT_HOST))
redirect307tr.Processes.Default.StartBefore(Test.Processes.ts)
redirect307tr.StillRunningAfter = ts
redirect307tr.Processes.Default.Command="python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_HOST))
redirect307tr.Processes.Default.TimeOut=5 # seconds
redirect307tr.Processes.Default.ReturnCode=0
redirect307tr.Processes.Default.Streams.stdout="redirect307_get.gold"

passthroughtr=Test.AddTestRun("Test domain {0}".format(PASSTHRU_HOST))
passthroughtr.StillRunningBefore = ts
passthroughtr.StillRunningAfter = ts
passthroughtr.Processes.Default.Command="python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(PASSTHRU_HOST))
passthroughtr.Processes.Default.TimeOut=5 # seconds
passthroughtr.Processes.Default.ReturnCode=0
passthroughtr.Processes.Default.Streams.stdout="passthrough_get.gold"
