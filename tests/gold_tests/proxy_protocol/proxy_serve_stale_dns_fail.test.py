'''
Test proxy serving stale content when DNS lookup fails
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

Test.ContinueOnFail = True
# Set up hierarchical caching processes
ts_child = Test.MakeATSProcess("ts_child")
ts_parent = Test.MakeATSProcess("ts_parent")
nameserver = Test.MakeDNServer("dns")
server_name = "http://unknown.domain.com/"

Test.testName = "STALE"

# Config child proxy to route to parent proxy
ts_child.Disk.records_config.update({
    'proxy.config.http.push_method_enabled': 1,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.http.cache.max_stale_age': 10,
    'proxy.config.http.parent_proxy.self_detect': 0,
    'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
})
ts_child.Disk.parent_config.AddLine(
    f'dest_domain=. parent=localhost:{ts_parent.Variables.port} round_robin=consistent_hash go_direct=false'
)
ts_child.Disk.remap_config.AddLine(
    f'map http://localhost:{ts_child.Variables.port} {server_name}'
)

# Configure parent proxy
ts_parent.Disk.records_config.update({
    'proxy.config.http.push_method_enabled': 1,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.http.cache.max_stale_age': 10,
    'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
})
ts_parent.Disk.remap_config.AddLine(
    f'map http://localhost:{ts_parent.Variables.port} {server_name}'
)
ts_parent.Disk.remap_config.AddLine(
    f'map {server_name} {server_name}'
)

# Configure nameserver
nameserver.addRecords(records={"localhost": ["127.0.0.1"]})

# Object to push to proxies
stale_5 = "HTTP/1.1 200 OK\nServer: ATS/10.0.0\nAccept-Ranges: bytes\nContent-Length: 6\nCache-Control: public, max-age=5\n\nCACHED"
stale_10 = "HTTP/1.1 200 OK\nServer: ATS/10.0.0\nAccept-Ranges: bytes\nContent-Length: 6\nCache-Control: public, max-age=10\n\nCACHED"


# Testing scenarios
child_curl_request = (
    # Test child serving stale with failed DNS OS lookup
    f'curl -X PUSH -d "{stale_5}" "http://localhost:{ts_child.Variables.port}";'
    f'curl -X PUSH -d "{stale_10}" "http://localhost:{ts_parent.Variables.port}";'
    f'sleep 7; curl -s -v http://localhost:{ts_child.Variables.port};'
    f'sleep 15; curl -s -v http://localhost:{ts_child.Variables.port};'
    # Test parent serving stale with failed DNS OS lookup
    f'curl -X PUSH -d "{stale_5}" "http://localhost:{ts_parent.Variables.port}";'
    f'sleep 7; curl -s -v http://localhost:{ts_parent.Variables.port};'
    f'sleep 15; curl -s -v http://localhost:{ts_parent.Variables.port};'
)

# Test case for when parent server is down but child proxy can serve cache object
tr = Test.AddTestRun()
tr.Processes.Default.Command = child_curl_request
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts_child)
tr.Processes.Default.StartBefore(ts_parent)
tr.Processes.Default.StartBefore(nameserver)
tr.Processes.Default.Streams.stderr = "gold/serve_stale_dns_fail.gold"
tr.StillRunningAfter = ts_child
tr.StillRunningAfter = ts_parent
