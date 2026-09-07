'''
Test that the rate_limit YAML metrics node names metrics as prefix.type.tag.
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
rate_limit: the YAML metrics node builds names as prefix.type.tag.
'''

Test.SkipUnless(Condition.PluginExists('rate_limit.so'))
Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")
server.addResponse(
    "sessionlog.json",
    {"headers": "GET /health HTTP/1.1\r\nHost: metrics.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""},
    {"headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "OK"},
)

ts = Test.MakeATSProcess("ts")

# Three selectors: both keys set, only the tag set, and only the prefix set.
# The defaults are the metric prefix and the SNI respectively, so a swap is
# visible in every one of them.
rate_limit_yaml = os.path.join(ts.Variables.CONFIGDIR, 'rate_limit.yaml')
ts.Disk.File(rate_limit_yaml, typename="ats:config").AddLines(
    [
        'selector:',
        '  - sni: both.example.com',
        '    limit: 100',
        '    metrics:',
        '      prefix: myprefix',
        '      tag: mytag',
        '  - sni: tagonly.example.com',
        '    limit: 100',
        '    metrics:',
        '      tag: onlytag',
        '  - sni: prefixonly.example.com',
        '    limit: 100',
        '    metrics:',
        '      prefix: onlyprefix',
        '',
    ]
)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rate_limit',
    }
)

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')
ts.Disk.plugin_config.AddLine(f'rate_limit.so {rate_limit_yaml}')

# The metrics are registered by TSStatCreate while the config is parsed, so they
# are queryable as soon as ATS is up, with no traffic needed.
tr = Test.AddTestRun("Metric names use the configured prefix and tag")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'traffic_ctl metric match sni'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'myprefix.sni.mytag.queued', 'prefix and tag should keep their configured positions'
)
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('mytag.sni.myprefix', 'the prefix and tag must not be swapped')

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'plugin.rate_limiter.sni.onlytag.queued', 'an unset prefix should fall back to the default prefix'
)
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    'onlytag.sni.plugin.rate_limiter', 'the default prefix must not be used as the tag'
)

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'onlyprefix.sni.prefixonly.example.com.queued', 'an unset tag should fall back to the SNI'
)
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    'prefixonly.example.com.sni.onlyprefix', 'the SNI must not be used as the prefix'
)
