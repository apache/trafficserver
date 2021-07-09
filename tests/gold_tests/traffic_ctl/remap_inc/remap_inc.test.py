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
Test traffic_ctl config reload with remap.config .include directive
'''

Test.ContinueOnFail = False

Test.Setup.Copy("wait_reload.sh")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server", enable_cache=False, dump_runroot=True)

ts.Disk.File(ts.Variables.CONFIGDIR + "/test.inc", id="test_cfg", typename="ats:config")
ts.Disk.test_cfg.AddLine(
    "map http://example.two/ http://yada.com/ " +
    "@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1"
)

ts.Disk.remap_config.AddLine(
    "map http://example.one/ http://yada.com/"
)
ts.Disk.remap_config.AddLine(
    ".include test.inc"
)
ts.Disk.remap_config.AddLine(
    "map http://example.three/ http://yada.com/"
)

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'regex_remap|url_rewrite|plugin_factory',
})

tr = Test.AddTestRun("Start TS, then update test.inc")
tr.Processes.Default.StartBefore(Test.Processes.ts)
test_inc_path = ts.Variables.CONFIGDIR + "/test.inc"
tr.Processes.Default.Command = (
    f"rm -f {test_inc_path} ; " +
    f"echo 'map http://example.four/ http://localhost/ @plugin=generator.so' > {test_inc_path}"
)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Reload config")
tr.StillRunningAfter = ts
tr.Processes.Default.Command = f'traffic_ctl config reload --run-root {ts.Disk.runroot_yaml.Name}'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Wait for config reload")
tr.Processes.Default.Command = './wait_reload.sh ' + os.path.join(ts.Variables.LOGDIR, 'diags.log')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Get response from generator")
tr.Processes.Default.Command = (
    f'test $$(curl --proxy 127.0.0.1:{ts.Variables.port} http://example.four/nocache/5 | wc -c) == 5'
)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
