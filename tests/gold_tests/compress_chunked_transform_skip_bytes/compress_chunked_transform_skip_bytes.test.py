'''Verify no crash when cache-write skip_bytes with transform and chunked origin.'''

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

import sys
from ports import get_port

Test.Summary = '''
Ensure cache-write consumer does not assert on skip_bytes when a transform is
present (compress plugin) and origin response is chunked.
'''

# Skip if compress plugin not present.
Test.SkipUnless(Condition.PluginExists('compress.so'))

tr = Test.AddTestRun('compress + chunked origin + cache write should not crash')
# Give ATS and the origin ample time to become ready on slower CI
tr.TimeOut = 60

# Origin server that responds with chunked-encoding and cacheable headers.
tr.Setup.Copy('server_chunked.sh')
server = tr.Processes.Process('chunked_origin')
server_port = get_port(server, 'http_port')
server.Command = f"bash server_chunked.sh {server_port} outserver"
server.ReturnCode = 0
server.Ready = When.PortOpen(server_port)

# ATS with cache enabled, remap to origin, compress plugin enabled (gzip only).
ts = tr.MakeATSProcess('ts', enable_cache=True)
# Explicitly mark ATS ready when its HTTP port is open
ts.Ready = When.PortOpen(ts.Variables.port)

# Minimal debug to help triage if it fails.
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|http_tunnel|cache|compress',
    })

# Provide a compress config that only enables gzip to avoid brotli dependency.
tr.Setup.Copy('compress_gzip_only.config')

ts.Disk.remap_config.AddLine(
    f"map http://test/ http://127.0.0.1:{server_port}/ @plugin=compress.so @pparam={Test.RunDirectory}/compress_gzip_only.config")

# Simple client request that should trigger compression and exercise the tunnel.
client = tr.Processes.Default
client.Command = (
    f"curl -sS --dump-header - --proxy http://127.0.0.1:{ts.Variables.port} "
    f"-H 'Accept-Encoding: gzip' http://test/obj && "
    f"curl -sS --dump-header - --proxy http://127.0.0.1:{ts.Variables.port} "
    f"-H 'Accept-Encoding: gzip' http://test/obj")
client.ReturnCode = 0

# Verify ATS served 200 and applied gzip encoding (plugin transform), which implies
# the tunnel ran without the skip_bytes assertion.
client.Streams.All += Testers.ContainsExpression('HTTP/1.1 200', 'Received 200 from ATS')
client.Streams.All += Testers.ContainsExpression('Content-Encoding: gzip', 'Response compressed by plugin')

# Startup ordering.
client.StartBefore(server)
client.StartBefore(ts)
