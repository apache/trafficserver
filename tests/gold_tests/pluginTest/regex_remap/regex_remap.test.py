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
import json
Test.Summary = '''
Test regex_remap
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
    Condition.PluginExists('regex_remap.so'),
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%uuid}")
server.addSessionFromFiles("replay")
replay = {}
with open(os.path.join(Test.TestDirectory, 'replay/yts-2819.replay.json')) as src:
    replay = json.load(src)

replay_txns = replay["sessions"][0]["transactions"]

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server")

testName = "regex_remap"

regex_remap_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_remap.conf')
curl_and_args = 'curl -s -D - -v --proxy localhost:{} '.format(ts.Variables.port)

path1_rule = 'path1 {}\n'.format(int(time.time()) + 600)

ts.Disk.File(regex_remap_conf_path, typename="ats:config").AddLines([
    "# regex_remap configuration\n"
    "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ http://example.one @status=301\n"
])

ts.Disk.remap_config.AddLine(
    "map http://example.one/ http://localhost:{}/ @plugin=regex_remap.so @pparam=regex_remap.conf\n".format(server.Variables.Port)
)

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|regex_remap',
    'proxy.config.http.cache.http': 0,
    'proxy.config.http.server_ports': '{}'.format(ts.Variables.port),
})

# 0 Test - Load cache (miss) (path1)
tr = Test.AddTestRun("smoke test")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
creq = replay_txns[0]['client-request']
tr.Processes.Default.Command = curl_and_args + '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + creq["url"]
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_smoke.gold"
tr.StillRunningAfter = ts

# Crash test.
tr = Test.AddTestRun("crash test")
creq = replay_txns[1]['client-request']
tr.Processes.Default.Command = curl_and_args + \
    '--header "uuid: {}" '.format(creq["headers"]["fields"][1][1]) + '"{}"'.format(creq["url"])
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_remap_crash.gold"
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    'ERROR: .regex_remap. Bad regular expression result -21', "Resource limit exceeded")
tr.StillRunningAfter = ts
