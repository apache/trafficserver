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

from tools.uranium.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_http204_response_plugin(urtest: UraniumTest) -> None:
    '''
    Tests that plugins may break HTTP by sending 204 response bodies
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

    urtest.Summary = '''
    Tests that plugins may break HTTP by sending 204 response bodies
    '''

    ts = urtest.MakeATSProcess("ts")
    server = urtest.MakeOriginServer("server")

    CUSTOM_PLUGIN_204_HOST = 'www.customplugin204.test'

    regex_remap_conf_file = "maps.reg"

    ts.Disk.remap_config.AddLine(
        f'map http://{CUSTOM_PLUGIN_204_HOST} http://127.0.0.1:{server.Variables.Port} @plugin=regex_remap.so @pparam={regex_remap_conf_file} @pparam=no-query-string @pparam=host'
    )
    ts.Disk.MakeConfigFile(regex_remap_conf_file).AddLine('//.*/ http://donotcare.test @status=204')

    urtest.PrepareTestPlugin(os.path.join(urtest.Variables.AtsTestPluginsDir, 'custom204plugin.so'), ts)

    urtest.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
    urtest.Setup.Copy('data')

    tr = urtest.AddTestRun("Test domain {0}".format(CUSTOM_PLUGIN_204_HOST))
    tr.Processes.Default.StartBefore(urtest.Processes.ts)
    tr.StillRunningAfter = ts

    tr.Processes.Default.Command = f"{sys.executable} tcp_client.py 127.0.0.1 {ts.Variables.port} data/{CUSTOM_PLUGIN_204_HOST}_get.txt"
    tr.Processes.Default.TimeOut = 5  # seconds
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "gold/http-204-custom-plugin.gold"
    urtest.execute()
