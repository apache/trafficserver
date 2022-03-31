'''
Verify that caching a range request when origin returns full response works.
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
Verify correct caching behavior for range requests.
'''

ts = Test.MakeATSProcess("ts")
replay_file = "replay/cache-range-response.replay.yaml"
server = Test.MakeVerifierServerProcess("server0", replay_file)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http.*|cache.*',
    'proxy.config.http.cache.range.write': 1,
    'proxy.config.http.cache.when_to_revalidate': 4,
    'proxy.config.http.insert_response_via_str': 3,
})
ts.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{server.Variables.http_port}'
)
tr = Test.AddTestRun("Verify range request is transformed from a 200 response")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client0", replay_file, http_ports=[ts.Variables.port])
