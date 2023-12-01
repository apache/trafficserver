'''
Test negative caching.
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
Test negative caching.
'''

#
# Negative caching disabled.
#
ts = Test.MakeATSProcess("ts-disabled")
replay_file = "replay/negative-caching-disabled.replay.yaml"
server = Test.MakeVerifierServerProcess("server-disabled", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 0
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify correct behavior without negative caching enabled.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-disabled", replay_file, http_ports=[ts.Variables.port])

#
# Negative caching enabled with otherwise default configuration.
#
ts = Test.MakeATSProcess("ts-default")
replay_file = "replay/negative-caching-default.replay.yaml"
server = Test.MakeVerifierServerProcess("server-default", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 1
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify default negative caching behavior")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-default", replay_file, http_ports=[ts.Variables.port])

#
# Customized response caching for negative caching configuration.
#
ts = Test.MakeATSProcess("ts-customized")
replay_file = "replay/negative-caching-customized.replay.yaml"
server = Test.MakeVerifierServerProcess("server-customized", replay_file)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 1,
        'proxy.config.http.negative_caching_list': "400"
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
tr = Test.AddTestRun("Verify customized negative caching list")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-customized", replay_file, http_ports=[ts.Variables.port])

#
# Verify correct proxy.config.http.negative_caching_lifetime behavior.
#
ts = Test.MakeATSProcess("ts-lifetime")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 1,
        'proxy.config.http.negative_caching_lifetime': 2
    })
# This should all behave the same as the default enabled case above.
tr = Test.AddTestRun("Add a 404 response to the cache")
replay_file = "replay/negative-caching-default.replay.yaml"
server = tr.AddVerifierServerProcess("server-lifetime-no-cc", replay_file)
# Use the same port across the two servers so that the remap config will work
# across both.
server_port = server.Variables.http_port
tr.AddVerifierClientProcess("client-lifetime-no-cc", replay_file, http_ports=[ts.Variables.port])
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server_port))
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Wait enough time that the item should be aged out of the cache.
tr = Test.AddTestRun("Wait for cached object to be stale.")
tr.Processes.Default.Command = "sleep 4"
tr.StillRunningAfter = ts

# Verify the item is retrieved from the server instead of the cache.
replay_file = "replay/negative-caching-timeout.replay.yaml"
tr = Test.AddTestRun("Make sure object is stale")
tr.AddVerifierServerProcess("server-timeout", replay_file, http_ports=[server_port])
tr.AddVerifierClientProcess("client-timeout", replay_file, http_ports=[ts.Variables.port])
tr.StillRunningAfter = ts

#
# Verify that the server's Cache-Control overrides the
# proxy.config.http.negative_caching_lifetime.
#
ts = Test.MakeATSProcess("ts-lifetime-2")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 1,
        'proxy.config.http.negative_caching_lifetime': 2
    })
tr = Test.AddTestRun("Add a 404 response with explicit max-age=300 to the cache")
replay_file = "replay/negative-caching-300-second-timeout.replay.yaml"
server = tr.AddVerifierServerProcess("server-lifetime-cc", replay_file)
# Use the same port across the two servers so that the remap config will work
# across both.
server_port = server.Variables.http_port
tr.AddVerifierClientProcess("client-lifetime-cc", replay_file, http_ports=[ts.Variables.port])
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server_port))
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Wait enough time that the item should be aged out of the cache if
# proxy.config.http.negative_caching_lifetime is incorrectly used.
tr = Test.AddTestRun("Wait for cached object to be stale if lifetime is incorrectly used.")
tr.Processes.Default.Command = "sleep 4"
tr.StillRunningAfter = ts

# Verify the item is retrieved from the cache instead of going to the origin.
replay_file = "replay/negative-caching-no-timeout.replay.yaml"
tr = Test.AddTestRun("Make sure object is fresh")
tr.AddVerifierServerProcess("server-no-timeout", replay_file, http_ports=[server_port])
tr.AddVerifierClientProcess("client-no-timeout", replay_file, http_ports=[ts.Variables.port])
tr.StillRunningAfter = ts
