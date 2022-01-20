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
Test forcing error responses on various TXN hooks.
'''

Test.ContinueOnFail = True

plugin_name = "pi_set_err_status"

# Disable the cache to make sure each request is forwarded to the origin
# server.
ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)

dns = Test.MakeDNServer("dns")

dns.addRecords(records={"test-host.com": ["127.0.0.1"]})

ts.Disk.records_config.update({
    'proxy.config.url_remap.remap_required': 0,
    'proxy.config.dns.nameservers': f'127.0.0.1:{dns.Variables.Port}',
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': f'http|{plugin_name}',
})

rp = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', plugin_name, '.libs', plugin_name + '.so')
ts.Setup.Copy(rp, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])

ts.Disk.plugin_config.AddLine(plugin_name + '.so')

ts.Setup.Copy('run_curl.sh')

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(dns)
# Make run_curl.sh executable.
tr.Processes.Default.Command = "bash -c 'chmod +x run_curl.sh'"
tr.Processes.Default.ReturnCode = 0


def addRuns(http_status):
    def add2Runs(hook_name, http_status):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            f'./run_curl.sh --verbose --ipv4 --header "X-Test-Data: {hook_name}/{http_status}" ' +
            f'--header "Host: test-host.com" http://localhost:{ts.Variables.port}/'
        )
        tr.Processes.Default.ReturnCode = 0

        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            f'./run_curl.sh --verbose --ipv4 --header "X-Test-Data: {hook_name}/{http_status}/'
            f'body for hook {hook_name}, status {http_status}" ' +
            f'--header "Host: test-host.com" http://localhost:{ts.Variables.port}/'
        )
        tr.Processes.Default.ReturnCode = 0

    add2Runs("READ_REQUEST_HDR", http_status)
    add2Runs("PRE_REMAP", http_status)
    add2Runs("POST_REMAP", http_status)
    add2Runs("CACHE_LOOKUP_COMPLETE", http_status)
    add2Runs("SEND_RESPONSE_HDR", http_status)


addRuns(400)
addRuns(403)
addRuns(451)

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'echo check output'
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File('stdout')
f.Content = 'stdout.gold'
f = tr.Disk.File('stderr')
f.Content = 'stderr.gold'
