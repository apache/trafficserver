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

from uranium_testkit.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_uri(urtest: UraniumTest) -> None:
    '''
    Verify correct URI parsing behavior.
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

    urtest.Summary = '''
    Verify correct URI parsing behavior.
    '''

    ts = urtest.MakeATSProcess("ts")
    replay_file = "uri.replay.yaml"
    server = urtest.MakeVerifierServerProcess("server", replay_file)
    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|cache|url',
    })
    ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
    tr = urtest.AddTestRun("Verify correct URI parsing behavior.")
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(ts)
    tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port])
    urtest.execute()
