'''
Test parent.config reload via ConfigRegistry.

Verifies that:
1. parent.config reload works after file touch
2. Record value change (retry_time) triggers parent reload
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
Test parent.config reload via ConfigRegistry.
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'parent_select|config',
})

# Initial parent.config with a simple rule
ts.Disk.parent_config.AddLine('dest_domain=example.com parent="origin.example.com:80"')

config_dir = ts.Variables.CONFIGDIR

# ================================================================
# Test 1: Touch parent.config → reload → handler fires
# ================================================================

tr = Test.AddTestRun("Touch parent.config")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"sleep 3 && touch {os.path.join(config_dir, 'parent.config')} && sleep 1"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Reload after parent.config touch")
p = tr.Processes.Process("reload-1")
p.Command = 'traffic_ctl config reload; sleep 30'
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
# Wait for the 2nd "finished loading" (1st is startup)
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "parent.config finished loading", 2)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
tr.Processes.Default.Command = 'echo "waiting for parent.config reload after file touch"'
tr.TimeOut = 25
tr.StillRunningAfter = ts

# ================================================================
# Test 2: Change retry_time record value → triggers parent reload
#         No file touch, no explicit config reload — the
#         RecRegisterConfigUpdateCb fires automatically.
# ================================================================

tr = Test.AddTestRun("Change parent retry_time record value")
p = tr.Processes.Process("reload-2")
p.Command = ("traffic_ctl config set proxy.config.http.parent_proxy.retry_time 60; "
             "sleep 30")
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
# Wait for the 3rd "finished loading"
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "parent.config finished loading", 3)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
## TODO: we should have an extension like When.ReloadCompleted(token, success) to validate this inetasd of parsing
##       diags.
tr.Processes.Default.Command = 'echo "waiting for parent.config reload after record change"'
tr.TimeOut = 25
tr.StillRunningAfter = ts
