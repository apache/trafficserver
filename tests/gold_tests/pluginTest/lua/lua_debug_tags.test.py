'''
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
Test lua is_debug_tag_set functionality
'''

Test.SkipUnless(
    Condition.PluginExists('tslua.so'),
)

Test.ContinueOnFail = False
# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager")

ts.Disk.remap_config.AddLine(
    'map http://test http://127.0.0.1/ @plugin=tslua.so @pparam=tags.lua'
)

# Configure the tslua's configuration file.
ts.Setup.Copy("tags.lua", ts.Variables.CONFIGDIR)
ts.Setup.Copy("tags.sh")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'foo|ts_lua',
})

curl_and_args = f'curl -s -D /dev/stderr -o /dev/stdout -x localhost:{ts.Variables.port} http://test/test.html'

# 0 Ensure no debug tag set
tr = Test.AddTestRun("check tags")
ps = tr.Processes.Default
tr.StillRunningAfter = ts
ps.StartBefore(Test.Processes.ts)
ps.Command = f"bash ./tags.sh {curl_and_args}"
ps.Env = ts.Env
ps.ReturnCode = 0
