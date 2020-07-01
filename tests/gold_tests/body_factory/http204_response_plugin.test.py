'''
Tests that plugins may break HTTP by sending 204 respose bodies
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
Tests that plugins may break HTTP by sending 204 respose bodies
'''

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

CUSTOM_PLUGIN_204_HOST = 'www.customplugin204.test'

regex_remap_conf_file = "maps.reg"

ts.Disk.remap_config.AddLine(
    'map http://{0} http://127.0.0.1:{1} @plugin=regex_remap.so @pparam={2} @pparam=no-query-string @pparam=host'
    .format(CUSTOM_PLUGIN_204_HOST, server.Variables.Port,
            regex_remap_conf_file)
)
ts.Disk.MakeConfigFile(regex_remap_conf_file).AddLine('//.*/ http://donotcare.test @status=204')

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'custom204plugin.so'), ts)

Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

tr = Test.AddTestRun("Test domain {0}".format(CUSTOM_PLUGIN_204_HOST))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

tr.Processes.Default.Command = "python3 tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get.txt'.format(CUSTOM_PLUGIN_204_HOST))
tr.Processes.Default.TimeOut = 5  # seconds
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/http-204-custom-plugin.gold"
