'''
Verify that stats_over_http compresses responses with many metrics and releases the intercept of aborted requests.
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
import shutil
import sys

Test.Summary = 'Compress stats_over_http responses with many metrics'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())
Test.ContinueOnFail = True

# With this many metrics, the Prometheus output is larger than the default 1 MiB thread stack.
METRIC_COUNT = 10000

# LeakSanitizer does not run on macOS, so cont_count.py counts live continuations to detect a leaked intercept.
ts = Test.MakeATSProcess('ts', command='traffic_server --disable_pfreelist')
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts, f'--count={METRIC_COUNT}')
ts.Disk.plugin_config.AddLine('stats_over_http.so _stats')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'test_metrics',
        'proxy.config.dump_mem_info_frequency': 1,
    })
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f'Created {METRIC_COUNT} metrics', 'test_metrics.so should create the requested gauges.')

client = os.path.join(Test.RunDirectory, 'fetch_stats.py')
Test.Setup.Copy('fetch_stats.py')
cont_count = os.path.join(Test.RunDirectory, 'cont_count.py')
Test.Setup.Copy('cont_count.py')
cont_baseline = os.path.join(Test.RunDirectory, 'cont_baseline')

encodings = ['gzip', 'deflate']
# fetch_stats.py decodes br with the brotli command.
if Test.Variables.get('TS_HAS_BROTLI') and shutil.which('brotli'):
    encodings.append('br')

cases = [(stats_format, encoding) for stats_format in ['json', 'csv', 'prometheus', 'prometheus_v2'] for encoding in encodings]
for i, (stats_format, encoding) in enumerate(cases):
    tr = Test.AddTestRun(f'Fetch {stats_format} stats with {encoding} encoding')
    if i == 0:
        tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.Command = (
        f'{sys.executable} {client} {ts.Variables.port} {stats_format} --encoding={encoding} --count={METRIC_COUNT}')
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        f'Verified {METRIC_COUNT} test metrics', f'The decoded {stats_format} body should hold every test metric.')
    tr.StillRunningAfter = ts

tr = Test.AddTestRun('Count the continuations in use before the aborted requests')
tr.Processes.Default.Command = f'{sys.executable} {cont_count} {ts.Disk.traffic_out.Name} {cont_baseline} --save'
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Send a request that Traffic Server rejects after the plugin sets up the intercept')
tr.Processes.Default.Command = f'{sys.executable} {client} {ts.Variables.port} json --reject --encoding=gzip'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'Response status 406', 'Traffic Server should reject the request.')
tr.StillRunningAfter = ts

for encoding in [None, 'gzip']:
    tr = Test.AddTestRun(f'Reset the client connection during a {encoding or "identity"} response')
    tr.Processes.Default.Command = (
        f'{sys.executable} {client} {ts.Variables.port} prometheus --reset' + (f' --encoding={encoding}' if encoding else ''))
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts

tr = Test.AddTestRun('Count the continuations in use after the aborted requests')
tr.Processes.Default.Command = f'{sys.executable} {cont_count} {ts.Disk.traffic_out.Name} {cont_baseline}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'no more than the baseline', 'The aborted requests should release their intercept continuations.')
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Fetch stats after the aborted requests')
tr.Processes.Default.Command = f'{sys.executable} {client} {ts.Variables.port} json --encoding=gzip --count={METRIC_COUNT}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    f'Verified {METRIC_COUNT} test metrics', 'Traffic Server should still serve stats.')
tr.StillRunningAfter = ts

# The plugin logs the aborted requests as errors, so allow only those.  The log lines show that the
# aborted requests reach the paths that release the intercept.
ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    r'ERROR: (?!\[stats_over_http\] stats_process_(read|write): Received TS_EVENT_)', 'diags.log should not contain other errors')
ts.Disk.diags_log.Content += Testers.ExcludesExpression('FATAL:', 'diags.log should not contain fatal errors')
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    'Unrecognized configuration value', 'diags.log should not warn about an unrecognized configuration')
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    'Received TS_EVENT_NET_ACCEPT_FAILED', 'The rejected request should reach the NET_ACCEPT_FAILED path.')
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    'stats_process_write: Received TS_EVENT_ERROR', 'A reset client connection should reach the write error path.')
