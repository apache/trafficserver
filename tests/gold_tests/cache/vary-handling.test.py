'''
Test correct handling of alternates via the Vary header.
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
Test correct handling of alternates via the Vary header.
'''

ts = Test.MakeATSProcess("ts")
replay_file = "replay/varied_transactions.replay.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.cache.limits.http.max_alts': 4,
        'proxy.config.cache.log.alternate.eviction': 1,
    })

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))

tr = Test.AddTestRun("Run traffic with max_alts behavior when set to 4")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port])

ts.Disk.diags_log.Content += "gold/two_alternates_evicted.gold"
