'''
Test the negative revalidating feature.
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

Test.Summary = '''
Test the negative revalidating feature.
'''


class NegativeRevalidatingTest:
    _test_num: int = 0

    def __init__(self, name: str, records_config: dict, replay_file: str):
        self._tr = Test.AddTestRun(name)
        self._replay_file = replay_file

        self.__setupOriginServer()
        self.__setupTS(records_config)
        self.__setupClient()

        NegativeRevalidatingTest._test_num += 1

    def __setupClient(self):
        self._tr.AddVerifierClientProcess(
            f"client-{NegativeRevalidatingTest._test_num}", self._replay_file, http_ports=[self._ts.Variables.port])

    def __setupOriginServer(self):
        self._server = self._tr.AddVerifierServerProcess(f"server-{NegativeRevalidatingTest._test_num}", self._replay_file)

    def __setupTS(self, records_config):
        self._ts = Test.MakeATSProcess(f"ts-{NegativeRevalidatingTest._test_num}")
        self._ts.Disk.records_config.update(records_config)
        self._ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(self._server.Variables.http_port))

    def run(self):
        self._tr.Processes.Default.StartBefore(self._ts)
        self._tr.StillRunningAfter = self._ts


#
# Verify disabled negative_revalidating behavior.
#
NegativeRevalidatingTest(
    "Verify negative revalidating disabled", {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|cache',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.insert_response_via_str': 2,
        'proxy.config.http.negative_revalidating_enabled': 0,
        'proxy.config.http.cache.max_stale_age': 6
    }, "replay/negative-revalidating-disabled.replay.yaml").run()

#
# Verify enabled negative_revalidating behavior.
#
NegativeRevalidatingTest(
    "Verify negative revalidating enabled",
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|cache',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.insert_response_via_str': 2,

        # Negative revalidating is on by default. Verify this by leaving out the
        # following line and expect negative_revalidating to be enabled.
        # 'proxy.config.http.negative_revalidating_enabled': 1,
        'proxy.config.http.cache.max_stale_age': 6
    },
    "replay/negative-revalidating-enabled.replay.yaml").run()

#
# Verify negative_revalidating list behavior.
#
NegativeRevalidatingTest(
    "Verify negative_revalidating_list behavior", {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|cache',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.insert_response_via_str': 2,
        'proxy.config.http.cache.max_stale_age': 6,
        'proxy.config.http.negative_revalidating_enabled': 1,
        'proxy.config.http.negative_revalidating_list': "403 404"
    }, "replay/negative-revalidating-list.replay.yaml").run()
