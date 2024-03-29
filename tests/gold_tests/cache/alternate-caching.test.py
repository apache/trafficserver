'''
Test the alternate caching features
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

Test.Summary = '''
Test the alternate caching feature.
'''

#
# Verify disabled negative_revalidating behavior.
#
ts = Test.MakeATSProcess("ts-alternate-caching")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|cache',
        'proxy.config.http.cache.max_stale_age': 6,
        'proxy.config.cache.select_alternate': 1,
        'proxy.config.cache.limits.http.max_alts': 4,

        # Try with and without this
        'proxy.config.http.negative_revalidating_enabled': 1,
        'proxy.config.http.negative_caching_enabled': 1,
        'proxy.config.http.negative_caching_lifetime': 30
    })
tr = Test.AddTestRun("Verify disabled negative revalidating behavior.")
replay_file = "replay/alternate-caching-update-size.yaml"
server = tr.AddVerifierServerProcess("server1", replay_file)
server_port = server.Variables.http_port
tr.AddVerifierClientProcess("client1", replay_file, http_ports=[ts.Variables.port])
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server_port))
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts
