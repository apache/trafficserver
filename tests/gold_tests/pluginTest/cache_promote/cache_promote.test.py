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

import os

Test.Summary = '''
Test cache_promote plugin
'''

Test.SkipUnless(Condition.PluginExists('cache_promote.so'))


class CachePromotePluginTest:

    def __init__(self):
        self._tr = Test.AddTestRun("cache_promote")
        self.__setupOriginServer()
        self.__setupTS()
        self.__setupClient()

    def __setupClient(self):
        # Eval template file to set origin server port to redirect
        template_file_path = os.path.join(self._tr.TestDirectory, "replay/cache_promote.replay.yaml.tmpl")
        with open(template_file_path, 'r') as f:
            template = f.read()

        replay_yaml_path = os.path.join(self._tr.RunDirectory, "cache_promote.replay.yaml")
        with open(replay_yaml_path, 'w') as f:
            f.write(template.format(httpbin_port=self._httpbin.Variables.Port))

        self._tr.AddVerifierClientProcess("verifier-client", replay_yaml_path, http_ports=[self._ts.Variables.port])

    def __setupOriginServer(self):
        self._httpbin = Test.MakeHttpBinServer("httpbin")

    def __setupTS(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=True)
        self._ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|cache_promote",
                "proxy.config.http.number_of_redirections": 1,  # follow redirect
                "proxy.config.http.redirect.actions": "self:follow",  # redirects to self are not followed by default
            })
        self._ts.Disk.plugin_config.AddLine("xdebug.so --enable=x-cache,x-cache-key")
        self._ts.Disk.remap_config.AddLines(
            {
                f"""
map /test_0/ http://127.0.0.1:{self._httpbin.Variables.Port}/ \
    @plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000

map /test_1/ http://127.0.0.1:{self._httpbin.Variables.Port}/ \
    @plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000 @pparam=--disable-on-redirect \
    @plugin=cachekey.so @pparam=--static-prefix=trafficserver.apache.org/443
"""
            })

    def run(self):
        self._tr.Processes.Default.StartBefore(self._ts)
        self._tr.Processes.Default.StartBefore(self._httpbin)
        self._tr.StillRunningAfter = self._ts
        self._tr.StillRunningAfter = self._httpbin


CachePromotePluginTest().run()
