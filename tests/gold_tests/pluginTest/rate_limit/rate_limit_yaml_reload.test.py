'''
Test that a malformed rate_limit YAML file fails the reload instead of
terminating traffic_server.
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
import shlex

Test.Summary = '''
rate_limit: a malformed YAML config fails the reload without killing ATS.
'''

Test.SkipUnless(Condition.PluginExists('rate_limit.so'))
Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /health HTTP/1.1\r\nHost: reload.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "OK"
    })

ts = Test.MakeATSProcess("ts")

rate_limit_yaml = os.path.join(ts.Variables.CONFIGDIR, 'rate_limit.yaml')
ts.Disk.File(
    rate_limit_yaml, typename="ats:config").AddLines([
        'selector:',
        '  - sni: reload.example.com',
        '    limit: 100',
        '',
    ])

# A selector entry with no "sni" key. Reading sni["sni"] on the const node
# throws YAML::InvalidNode before the "without a name" check can report it.
missing_sni = os.path.join(Test.RunDirectory, 'missing_sni.yaml')
with open(missing_sni, 'w') as f:
    f.write('selector:\n  - limit: 100\n')

# "percentage" is read as a uint32_t, so the fractional value that the
# documentation used to suggest throws YAML::TypedBadConversion.
bad_percentage = os.path.join(Test.RunDirectory, 'bad_percentage.yaml')
with open(bad_percentage, 'w') as f:
    f.write('ip-rep:\n  - name: test\n    size: 15\n    percentage: 0.9\n')

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rate_limit',
})

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')
ts.Disk.plugin_config.AddLine(f'rate_limit.so {rate_limit_yaml}')

BASE_URL = f"http://127.0.0.1:{ts.Variables.port}/health"
CURL = f"curl -s -o /dev/null -w '%{{http_code}}' -H 'Host: reload.example.com' '{BASE_URL}'"

tr = Test.AddTestRun("Start with a valid config")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = CURL
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "ATS should serve the request")
tr.StillRunningAfter = ts

# The plugin callback only fires when the file mtime advances, so sleep before
# each overwrite to make sure it does.
for description, bad_config in [("selector entry without an sni key", missing_sni), ("a fractional percentage", bad_percentage)]:
    tr = Test.AddTestRun(f"Install {description}")
    tr.Processes.Default.Command = f"sleep 2 && cp {shlex.quote(bad_config)} {shlex.quote(rate_limit_yaml)}"
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts

    Test.AddConfigReload(ts, delay_start=1, description=f"Reload with {description}")

    tr = Test.AddTestRun(f"ATS survives {description}")
    tr.Processes.Default.Command = f"sleep 2 && {CURL}"
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "ATS should still be serving traffic")
    tr.StillRunningAfter = ts

# Both cases have to be reported and rejected. Assigning here replaces the
# default "diags.log has no ERROR:" testers, since these errors are expected.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "selector node is not a map or without a name", "The missing sni key should be reported, not thrown")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Failed to parse configuration file", "The bad percentage should be caught by the parser")
ts.Disk.diags_log.Content += Testers.ContainsExpression("Failed to reload YAML file", "Both reloads should be rejected")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL", "ATS should not die on a malformed config")
