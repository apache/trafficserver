'''
Test cache.config and hosting.config reload via ConfigRegistry.

Verifies that:
1. cache.config reload works after file touch
2. hosting.config reload works after file touch (requires cache to be initialized)
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
Test cache.config and hosting.config reload via ConfigRegistry.
'''

Test.ContinueOnFail = True

# Create ATS with cache enabled (needed for hosting.config registration in open_done)
ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|config',
})

# Set up initial cache.config with a caching rule
ts.Disk.cache_config.AddLine('dest_domain=example.com ttl-in-cache=30d')

config_dir = ts.Variables.CONFIGDIR

# --- Test 1: Touch cache.config and reload ---

tr = Test.AddTestRun("Touch cache.config to trigger change detection")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"touch {os.path.join(config_dir, 'cache.config')} && sleep 2"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Reload after cache.config touch")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl config reload --show-details --token reload_cache_test'
tr.Processes.Default.ReturnCode = Any(0, 2)
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("cache.config", "Reload output should reference cache.config")

# --- Test 2: Touch hosting.config and reload ---

tr = Test.AddTestRun("Touch hosting.config to trigger change detection")
tr.DelayStart = 3
tr.Processes.Default.Command = f"touch {os.path.join(config_dir, 'hosting.config')} && sleep 2"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Reload after hosting.config touch")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl config reload --show-details --token reload_hosting_test'
tr.Processes.Default.ReturnCode = Any(0, 2)
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("hosting.config", "Reload output should reference hosting.config")
