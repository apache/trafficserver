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
regex_revalidate plugin test, reload epoch state on ats start
'''

# Test description:
# Ensures that that the regex revalidate config file is loaded,
# then epoch times from the state file are properly merged.

Test.SkipUnless(
    Condition.PluginExists('regex_revalidate.so')
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager")

# **testname is required**
testName = "regex_revalidate_state"

# default root
request_header_0 = {"headers":
                    "GET / HTTP/1.1\r\n" +
                    "Host: www.example.com\r\n" +
                    "\r\n",
                    "timestamp": "1469733493.993",
                    "body": "",
                    }

response_header_0 = {"headers":
                     "HTTP/1.1 200 OK\r\n" +
                     "Connection: close\r\n" +
                     "Cache-Control: max-age=300\r\n" +
                     "\r\n",
                     "timestamp": "1469733493.993",
                     "body": "xxx",
                     }

server.addResponse("sessionlog.json", request_header_0, response_header_0)

reval_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'reval.conf')
reval_state_path = os.path.join(Test.Variables.RUNTIMEDIR, 'reval.state')

# Configure ATS server
ts.Disk.plugin_config.AddLine(
    f"regex_revalidate.so -d -c reval.conf -l reval.log -f {reval_state_path}"
)

sep = ' '

# rule with no initial state
path0_regex = "path0"
path0_expiry = str(time.time() + 90).split('.')[0]
path0_type = "STALE"
path0_rule = sep.join([path0_regex, path0_expiry, path0_type])

path1_regex = "path1"
path1_epoch = str(time.time() - 50).split('.')[0]
path1_expiry = str(time.time() + 600).split('.')[0]
path1_type = "MISS"
path1_rule = sep.join([path1_regex, path1_expiry, path1_type])

# Create gold files
gold_path_good = reval_state_path + ".good"
ts.Disk.File(gold_path_good, typename="ats:config").AddLines([
    sep.join([path0_regex, "``", path0_expiry, path0_type]),
    sep.join([path1_regex, path1_epoch, path1_expiry, path1_type]),
])

# It seems there's no API for negative gold file matching
'''
gold_path_bad = reval_state_path + ".bad"
ts.Disk.File(gold_path_bad, typename="ats:config").AddLines([
  sep.join([path0_regex, path1_epoch, path0_expiry, path0_type]),
  sep.join([path1_regex, path1_epoch, path1_expiry, path1_type]),
])
'''

# Create a state file, second line will be discarded and not merged
ts.Disk.File(reval_state_path, typename="ats:config").AddLines([
    sep.join([path1_regex, path1_epoch, path1_expiry, path1_type]),
    sep.join(["dummy", path1_epoch, path1_expiry, path1_type]),
])

# Write out reval.conf file
ts.Disk.File(reval_conf_path, typename="ats:config").AddLines([
    path0_rule, path1_rule,
])

ts.chownForATSProcess(reval_state_path)

ts.Disk.remap_config.AddLine(
    f"map http://ats/ http://127.0.0.1:{server.Variables.Port}"
)

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'regex_revalidate',
    'proxy.config.http.wait_for_cache': 1,
})


# This TestRun creates the state file so it exists when the ts process's Setup
# logic is run so that it can be chowned at that point.
tr = Test.AddTestRun("Populate the regex_revalidate state file")
tr.Processes.Default.Command = f'touch {reval_state_path}'

# Start ATS and evaluate the new state file
tr = Test.AddTestRun("Initial load, state merged")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))

# Note the ready condition: wait for ATS to modify the contents
# of the file from dummy to path1.
ps.StartBefore(Test.Processes.ts, ready=When.FileContains(reval_state_path, "path0"))

ps.Command = 'cat ' + reval_state_path
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.GoldFile(gold_path_good)
tr.StillRunningAfter = ts
