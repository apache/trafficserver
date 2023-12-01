'''
Verify correct caching behavior with respect to request method.
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
Verify correct caching behavior with respect to request method.
'''

# Test 0: Verify correct POST response handling when caching POST responses is
# disabled.
ts = Test.MakeATSProcess("ts")
replay_file = "replay/post_with_post_caching_disabled.replay.yaml"
server = Test.MakeVerifierServerProcess("server0", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http.*|cache.*',
        'proxy.config.http.insert_age_in_response': 0,

        # Caching of POST responses is disabled by default. Verify default behavior
        # by leaving it unconfigured.
        # 'proxy.config.http.cache.post_method': 0,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct with POST response caching disabled.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client0", replay_file, http_ports=[ts.Variables.port])

# Test 1: Verify correct POST response handling when caching POST responses is
# enabled.
ts = Test.MakeATSProcess("ts-cache-post")
replay_file = "replay/post_with_post_caching_enabled.replay.yaml"
server = Test.MakeVerifierServerProcess("server1", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http.*|cache.*',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.cache.post_method': 1,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct with POST response caching enabled.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client1", replay_file, http_ports=[ts.Variables.port])

# Test 2: Verify correct HEAD response handling with cached GET response
ts = Test.MakeATSProcess("ts-cache-head")
replay_file = "replay/head_with_get_cached.replay.yaml"
server = Test.MakeVerifierServerProcess("server2", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http.*|cache.*',
        'proxy.config.http.insert_age_in_response': 0,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct with HEAD response.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client2", replay_file, http_ports=[ts.Variables.port])
