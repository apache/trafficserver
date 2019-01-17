'''
Tests 30x responses are returned for matching domains
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
Tests 30x responses are returned for matching domains
'''

Test.SkipUnless(Condition.HasProgram("grep", "grep needs to be installed on system for this test to work"))

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

REDIRECT_301_HOST = 'www.redirect301.test'
REDIRECT_302_HOST = 'www.redirect302.test'
REDIRECT_307_HOST = 'www.redirect307.test'
REDIRECT_308_HOST = 'www.redirect308.test'
REDIRECT_0_HOST = 'www.redirect0.test'
PASSTHRU_HOST = 'www.passthrough.test'

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header_rewrite|dbg_header_rewrite',
    'proxy.config.body_factory.enable_logging': 1,
})

ts.Disk.remap_config.AddLine("""\
regex_map http://{0}/ http://{0}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules_301.conf
regex_map http://{1}/ http://{1}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules_302.conf
regex_map http://{2}/ http://{2}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules_307.conf
regex_map http://{3}/ http://{3}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules_308.conf
regex_map http://{4}/ http://{4}/ @plugin=header_rewrite.so @pparam=header_rewrite_rules_0.conf
""".format(REDIRECT_301_HOST, REDIRECT_302_HOST, REDIRECT_307_HOST, REDIRECT_308_HOST, REDIRECT_0_HOST)
)

for x in (0, 301, 302, 307, 308):
    ts.Disk.MakeConfigFile("header_rewrite_rules_{0}.conf".format(x)).AddLine("""\
set-redirect {0} "%{{CLIENT-URL}}"
""".format(x))

Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

redirect301tr = Test.AddTestRun("Test domain {0}".format(REDIRECT_301_HOST))
redirect301tr.Processes.Default.StartBefore(Test.Processes.ts)
redirect301tr.StillRunningAfter = ts
redirect301tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_301_HOST))
redirect301tr.Processes.Default.TimeOut = 5  # seconds
redirect301tr.Processes.Default.ReturnCode = 0
redirect301tr.Processes.Default.Streams.stdout = "redirect301_get.gold"

redirect302tr = Test.AddTestRun("Test domain {0}".format(REDIRECT_302_HOST))
redirect302tr.StillRunningBefore = ts
redirect302tr.StillRunningAfter = ts
redirect302tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_302_HOST))
redirect302tr.Processes.Default.TimeOut = 5  # seconds
redirect302tr.Processes.Default.ReturnCode = 0
redirect302tr.Processes.Default.Streams.stdout = "redirect302_get.gold"


redirect307tr = Test.AddTestRun("Test domain {0}".format(REDIRECT_307_HOST))
redirect302tr.StillRunningBefore = ts
redirect307tr.StillRunningAfter = ts
redirect307tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_307_HOST))
redirect307tr.Processes.Default.TimeOut = 5  # seconds
redirect307tr.Processes.Default.ReturnCode = 0
redirect307tr.Processes.Default.Streams.stdout = "redirect307_get.gold"

redirect308tr = Test.AddTestRun("Test domain {0}".format(REDIRECT_308_HOST))
redirect308tr.StillRunningBefore = ts
redirect308tr.StillRunningAfter = ts
redirect308tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_308_HOST))
redirect308tr.Processes.Default.TimeOut = 5  # seconds
redirect308tr.Processes.Default.ReturnCode = 0
redirect308tr.Processes.Default.Streams.stdout = "redirect308_get.gold"

redirect0tr = Test.AddTestRun("Test domain {0}".format(REDIRECT_0_HOST))
redirect0tr.StillRunningBefore = ts
redirect0tr.StillRunningAfter = ts
redirect0tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(REDIRECT_0_HOST))
redirect0tr.Processes.Default.TimeOut = 5  # seconds
redirect0tr.Processes.Default.ReturnCode = 0
redirect0tr.Processes.Default.Streams.stdout = "redirect0_get.gold"

passthroughtr = Test.AddTestRun("Test domain {0}".format(PASSTHRU_HOST))
passthroughtr.StillRunningBefore = ts
passthroughtr.StillRunningAfter = ts
passthroughtr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(PASSTHRU_HOST))
passthroughtr.Processes.Default.TimeOut = 5  # seconds
passthroughtr.Processes.Default.ReturnCode = 0
passthroughtr.Processes.Default.Streams.stdout = "passthrough_get.gold"

# Overriding the built in ERROR check since we expect some ERROR messages
ts.Disk.diags_log.Content = Testers.ContainsExpression("unsupported redirect status 0", "This test is a failure test")
