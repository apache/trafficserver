'''
Tests that 204 responses conform to rfc2616, unless custom templates override.
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

Test.Summary = '''
Tests that 204 responses conform to rfc2616, unless custom templates override.
'''

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

DEFAULT_204_HOST = 'www.default204.test'
CUSTOM_TEMPLATE_204_HOST = 'www.customtemplate204.test'

ts.Disk.records_config.update({
    # enable domain specific body factory
    'proxy.config.body_factory.enable_customizations': 3,
})

# Create a template body for a 204.
body_factory_dir = ts.Variables.BODY_FACTORY_TEMPLATE_DIR
ts.Disk.File(os.path.join(body_factory_dir, 'default', CUSTOM_TEMPLATE_204_HOST + '_default')).\
    WriteOn(
    """<HTML>
<HEAD>
<TITLE>Spec-breaking 204!</TITLE>
</HEAD>

<BODY BGCOLOR="white" FGCOLOR="black">
<H1>This is body content for a 204.</H1>
<HR>

<FONT FACE="Helvetica,Arial"><B>
Description: According to rfc7231 I should not have been sent to you!
</B></FONT>
<HR>
</BODY>
""")

regex_remap_conf_file = "maps.reg"

ts.Disk.remap_config.AddLine(
    'map http://{0} http://127.0.0.1:{1} @plugin=regex_remap.so @pparam={2} @pparam=no-query-string @pparam=host'
                    .format(DEFAULT_204_HOST, server.Variables.Port, regex_remap_conf_file)
)
ts.Disk.remap_config.AddLine(
    'map http://{0} http://127.0.0.1:{1} @plugin=regex_remap.so @pparam={2} @pparam=no-query-string @pparam=host @plugin=conf_remap.so @pparam=proxy.config.body_factory.template_base={0}'
                    .format(CUSTOM_TEMPLATE_204_HOST, server.Variables.Port, regex_remap_conf_file)
)
ts.Disk.MakeConfigFile(regex_remap_conf_file).AddLine(
    '//.*/ http://127.0.0.1:{0} @status=204'
    .format(server.Variables.Port)
)

Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

defaultTr = Test.AddTestRun("Test domain {0}".format(DEFAULT_204_HOST))
defaultTr.Processes.Default.StartBefore(Test.Processes.ts)
defaultTr.StillRunningAfter = ts

defaultTr.Processes.Default.Command = f"{sys.executable} tcp_client.py 127.0.0.1 {ts.Variables.port} data/{DEFAULT_204_HOST}_get.txt"
defaultTr.Processes.Default.TimeOut = 5  # seconds
defaultTr.Processes.Default.ReturnCode = 0
defaultTr.Processes.Default.Streams.stdout = "gold/http-204.gold"


customTemplateTr = Test.AddTestRun(f"Test domain {CUSTOM_TEMPLATE_204_HOST}")
customTemplateTr.StillRunningBefore = ts
customTemplateTr.StillRunningAfter = ts
customTemplateTr.Processes.Default.Command = f"{sys.executable} tcp_client.py 127.0.0.1 {ts.Variables.port} data/{CUSTOM_TEMPLATE_204_HOST}_get.txt"
customTemplateTr.Processes.Default.TimeOut = 5  # seconds
customTemplateTr.Processes.Default.ReturnCode = 0
customTemplateTr.Processes.Default.Streams.stdout = "gold/http-204-custom.gold"
