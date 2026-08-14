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


def test_log_plugin_init(urtest: UraniumTest) -> None:
    '''
    Verify text logging during plugin initialization.
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

    urtest.Summary = '''
    Verify plugins can fill a text log buffer before logging threads start.
    '''

    ts = urtest.MakeATSProcess('ts')
    ts.Disk.records_config.update({'proxy.config.log.log_buffer_size': 9216})
    urtest.PrepareTestPlugin(os.path.join(urtest.Variables.AtsTestPluginsDir, 'test_log_interface.so'), ts, '--write-during-init')

    plugin_log = urtest.Disk.File(os.path.join(ts.Variables.LOGDIR, 'test_log_interface.log'), exists=True)
    plugin_log.Content = Testers.ContainsExpression(
        'Writing during plugin initialization', 'The pre-initialization log buffer should be flushed')

    tr = urtest.AddTestRun('Start ATS with a plugin that fills a text log buffer during initialization')
    tr.Processes.Default.Command = 'printf "traffic_server remained running"'
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.StartBefore(ts)
    tr.StillRunningAfter = ts
    urtest.execute()
