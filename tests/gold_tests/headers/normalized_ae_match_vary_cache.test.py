'''
Test cache matching with the normalized Accept-Encoding header field
and the Vary header field in response
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

Test.Summary = '''
Test cache matching with the normalized Accept-Encoding and the Vary header field in response
'''

Test.ContinueOnFail = True

testName = "NORMALIZE_AE_MATCH_VARY"

replay_file = "replays/normalized_ae_varied_transactions.replay.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)

# Verify that cache hit requests never reach the server
# Case 2 (normalize_ae:1) cache hits
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 12", "Verify empty Accept-Encoding (uuid 12) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 13", "Verify deflate request (uuid 13) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 14", "Verify br,compress request (uuid 14) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 16", "Verify br,compress,gzip request (uuid 16) is a cache hit and doesn't reach the server.")
# Case 3 (normalize_ae:2) cache hits
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 22", "Verify empty Accept-Encoding (uuid 22) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 23", "Verify deflate request (uuid 23) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 26", "Verify br,compress,gzip request (uuid 26) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 27", "Verify compress,gzip request (uuid 27) is a cache hit and doesn't reach the server.")
# Case 4 (normalize_ae:3) cache hits
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 32", "Verify empty Accept-Encoding (uuid 32) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 33", "Verify deflate request (uuid 33) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 37", "Verify compress,gzip request (uuid 37) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 38", "Verify br;q=1.1 request (uuid 38) is a cache hit and doesn't reach the server.")
server.Streams.stdout += Testers.ExcludesExpression(
    "uuid: 39", "Verify br,gzip;q=0.8 request (uuid 39) is a cache hit and doesn't reach the server.")

ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.Disk.remap_config.AddLine(
    f"map http://www.ae-0.com http://127.0.0.1:{server.Variables.http_port}" +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=0')
ts.Disk.remap_config.AddLine(
    f"map http://www.ae-1.com http://127.0.0.1:{server.Variables.http_port}" +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=1')
ts.Disk.remap_config.AddLine(
    f"map http://www.ae-2.com http://127.0.0.1:{server.Variables.http_port}" +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=2')
# disable normalize_ae=3 on 9.1
ts.Disk.remap_config.AddLine(
    f"map http://www.ae-3.com http://127.0.0.1:{server.Variables.http_port}" +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=3')
ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.response_via_str': 3,
        # the following variables could affect the results of alternate cache matching,
        # define them with their default values explicitly
        'proxy.config.cache.limits.http.max_alts': 5,
        'proxy.config.http.cache.ignore_accept_mismatch': 2,
        'proxy.config.http.cache.ignore_accept_language_mismatch': 2,
        'proxy.config.http.cache.ignore_accept_encoding_mismatch': 2,
        'proxy.config.http.cache.ignore_accept_charset_mismatch': 2,
    })

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port])
tr.StillRunningAfter = ts
