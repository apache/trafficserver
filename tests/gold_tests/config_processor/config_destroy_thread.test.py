'''
Verify that a replaced config is destroyed on an ET_TASK thread.
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
Verify that a replaced config is destroyed on an ET_TASK thread.
'''

# ConfigProcessor::set() waits CONFIG_PROCESSOR_RELEASE_SECS before it releases the config that it
# replaced.  That timeout is a compile time constant of 60 seconds, so this test needs more than a
# minute of wall clock and does not run in CI.  Comment out the next line to run it.
Test.SkipIf(Condition.true("Test takes over 60 seconds to run."))

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'config',
})

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:8080')

config_dir = ts.Variables.CONFIGDIR

# Two replacements, reached two different ways.  Touching parent.config replaces ParentConfigParams
# through the reload framework, which runs on ET_TASK.  Changing an HTTP record replaces
# HttpConfigParams from a network thread, which is the case this test is really about.  Neither old
# config is referenced once the test stops sending traffic, so both reach a zero reference count and
# are destroyed when the release timeout expires.
tr = Test.AddTestRun("Mark parent.config for reload")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"sleep 3 && touch {os.path.join(config_dir, 'parent.config')} && sleep 1"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

Test.AddConfigReload(ts, expect="any", token="config_destroy_thread")

tr = Test.AddTestRun("Replace the HTTP config from a network thread")
tr.DelayStart = 3
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = "traffic_ctl config set proxy.config.http.response_server_str probe && sleep 3"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Wait for the release timeout to expire")
tr.DelayStart = 3
tr.Processes.Default.Command = "sleep 80"
tr.Processes.Default.ReturnCode = 0
tr.TimeOut = 150
tr.StillRunningAfter = ts

# The releaser runs on ET_TASK, so it destroys the replaced config there.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    r"Destroyed config \d+ in \d+ ns on thread \[ET_TASK", "a replaced config should be destroyed on a task thread")

# Destroying a config on a network thread is the regression this test guards against.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r"Destroyed config \d+ in \d+ ns on thread \[ET_NET", "no config should be destroyed on a network thread")
