'''
Test conf_reamp plugin
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the #  "License"); you may not use this file except in compliance
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
Test conf_remap plugin
'''

Test.SkipUnless(Condition.PluginExists('conf_remap.so'))


class ConfRemapPluginTest:

    def __init__(self):
        self._tr = Test.AddTestRun("conf_remap")
        self._replay_file = "replay/conf_remap.replay.yaml"

        self.__setupOriginServer()
        self.__setupTS()
        self.__setupClient()

    def __setupClient(self):
        self._tr.AddVerifierClientProcess("verifier-client", self._replay_file, http_ports=[self._ts.Variables.port])

    def __setupOriginServer(self):
        self._server = self._tr.AddVerifierServerProcess("verifier-server", self._replay_file)

    def __setupTS(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=True)
        self._ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.mode": 1,
                "proxy.config.diags.debug.tags": "http|conf_remap",
                "proxy.config.http.insert_response_via_str": 2,
                "proxy.config.http.negative_caching_enabled": 0
            })

        self._ts.Setup.Copy("etc/negative_caching_list.yaml")

        self._ts.Disk.remap_config.AddLines(
            {
                f"""
map /default_negative_caching_list/ \
    http://127.0.0.1:{self._server.Variables.http_port}/default_negative_caching_list/ \
    @plugin=conf_remap.so @pparam=proxy.config.http.negative_caching_enabled=1

map /custom_negative_caching_list/ \
    http://127.0.0.1:{self._server.Variables.http_port}/custom_negative_caching_list/ \
    @plugin=conf_remap.so @pparam={Test.RunDirectory}/negative_caching_list.yaml
    """
            })

    def run(self):
        self._tr.Processes.Default.StartBefore(self._ts)
        self._tr.StillRunningAfter = self._ts


ConfRemapPluginTest().run()
