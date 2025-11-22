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

# Negative caching disabled
Test.ATSReplayTest(replay_file="replay/negative-caching-disabled.replay.yaml")

# Negative caching enabled with default configuration
Test.ATSReplayTest(replay_file="replay/negative-caching-default.replay.yaml")

# Customized response caching for negative caching configuration
Test.ATSReplayTest(replay_file="replay/negative-caching-customized.replay.yaml")

#
# Verify correct proxy.config.http.negative_caching_lifetime behavior.
# These tests require multiple test runs with shared ATS processes, so use class-based approach.
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

#
# Verify that negative_caching_lifetime is respected even when cache.config
# has ttl-in-cache configured.
#
replay_file = "replay/negative-caching-ttl-in-cache.replay.yaml"
tr = Test.AddTestRun("Verify negative_caching_lifetime and ttl-in-cache interaction.")
dns = tr.MakeDNServer("dns", default="127.0.0.1")
server = tr.AddVerifierServerProcess("server-ttl-in-cache", replay_file)
server_port = server.Variables.http_port
ts = tr.MakeATSProcess("ts-ttl-in-cache")
ts.Disk.records_config.update(
    {
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.negative_caching_enabled': 1,
        'proxy.config.http.negative_caching_lifetime': 2
    })
ts.Disk.remap_config.AddLine(f'map / http://backend.example.com:{server_port}')
# Configure cache.config with a long ttl-in-cache that should NOT override
# negative_caching_lifetime for negative responses.
ts.Disk.cache_config.AddLine('dest_domain=backend.example.com ttl-in-cache=30d')
p = tr.AddVerifierClientProcess("client-ttl-in-cache", replay_file, http_ports=[ts.Variables.port])
p.StartBefore(dns)
p.StartBefore(server)
p.StartBefore(ts)
