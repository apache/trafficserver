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
import time
Test.Summary = '''
regex_revalidate plugin config file load test
'''

# Test description:
# Load up cache, ensure fresh
# Create regex reval rule, config reload:
#  ensure item is staled only once.
# Add a new rule, config reload:
#  ensure item isn't restaled again, but rule still in effect.
#
# If the rule disappears from regex_revalidate.conf its still loaded!!
# A rule's expiry can't be changed after the fact!

Test.SkipUnless(
    Condition.PluginExists('regex_revalidate.so'),
)
Test.ContinueOnFail = False

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager")

ts.Disk.plugin_config.AddLine(
    'regex_revalidate.so -d -c regex_revalidate.conf'
)

# **testname is required**
#testName = "regex_reval"

ts.Disk.remap_config.AddLine(
    'map http://ats/ http://localhost/'
)

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'regex_revalidate',
})

# create regex revalidate rule to load
regex_revalidate_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_revalidate.conf')

path1_expiry = int(time.time()) + 600
path1_rule = 'path1 {}\n'.format(path1_expiry)

# Define first revision for when trafficserver starts
ts.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule,
])

# 0 - unnecessary reload
tr = Test.AddTestRun("Reload unchanged file")
ps = tr.Processes.Default
ps.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 0 - load duplicate
tr = Test.AddTestRun("Reload duplicate config, new mtime")
tr.DelayStart = 2
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule)
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 1 - change expiry and reload
path1_expiry_new = path1_expiry + 50
path1_rule_new = 'path1 {}\n'.format(path1_expiry_new)

tr = Test.AddTestRun("Reload config, new expiry")
tr.DelayStart = 1
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule_new)
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 2 - old rule first, new rule last
tr = Test.AddTestRun("Reload config, no change")
tr.DelayStart = 1
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule,
    path1_rule_new,
])
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 3 - new rule first, old rule last
tr = Test.AddTestRun("Reload config, change to original")
tr.DelayStart = 1
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule_new,
    path1_rule,
])
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

path2_expiry = int(time.time()) + 200
path2_rule = 'path2 {}\n'.format(path2_expiry)

# 4 - different rule, old rule should still be in effect
tr = Test.AddTestRun("Reload config, just different rule")
tr.DelayStart = 1
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path2_rule)
tr.StillRunningAfter = ts
ps.Command = 'traffic_ctl plugin msg regex_revalidate config_reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 5 - stall so that logs can flush
tr = test.AddTestRun("Flush ats logs")
ps = tr.Processes.Default
ps.Command = "sleep 3"

expected_answers = ["false", "true", "false", "true", "true"]


def check_ats_logs(event, tester):
    with open(tester.GetContent(event)) as file:
        lines = file.readlines()
        newlines = []
        for line in lines:
            if "Rules have been changed:" in line:
                newlines.append(line.strip())
        if len(newlines) != len(expected_answers):
            res = 'expected {} lines, got {}'.format(len(expected), len(newlines))
            return (False, "config_reload lines", res)

        for ans, line in zip(expected_answers, newlines):
            if ans not in line:
                return (False, "config_reload lines", "wrong answer line {}".format(ind))
        return (True, "config_reload lines", "saw expected answers")


ts.Streams.stderr = Testers.Lambda(check_ats_logs)
