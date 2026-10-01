'''Verify bodyless HTTP/2 origin responses with header blocks over 4 KB.'''
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
import sys
from ports import get_port

Test.Summary = __doc__
Test.ContinueOnFail = True

# An HTTP/2 origin response that ends the stream on its HEADERS frame is handed
# to the HttpSM as one buffer spanning several 4 KB IOBuffer blocks, with EOS
# set. Every response must reach the client with the origin's status and
# headers rather than a 5xx or a truncated header.
#
# The origin is a Python script because Proxy Verifier will not send a header
# block over 64 KiB. Each case is (header block size, status, has body).
ORIGIN = os.path.join(Test.TestDirectory, 'h2_large_header_origin.py')
KB = 1024
CASES = [
    (3800, 200, False),
    (4300, 200, False),
    (9 * KB, 302, False),
    (9 * KB, 204, False),
    (9 * KB, 200, True),
    (16 * KB, 302, False),
    (64 * KB, 302, False),
    (128 * KB, 204, False),
    (256 * KB, 302, False),
    (512 * KB, 204, False),
    (900 * KB, 302, False),
    (512 * KB, 200, True),
]

origin = Test.Processes.Process("h2-large-header-origin")
origin_port = get_port(origin, "origin_port")
ssl_dir = os.path.join(Test.Variables.AtsTestToolsDir, "ssl")
origin.Command = (
    f"{sys.executable} {ORIGIN} serve 127.0.0.1 {origin_port}"
    f" --cert {os.path.join(ssl_dir, 'server.pem')} --key {os.path.join(ssl_dir, 'server.key')}")
origin.Ready = When.PortOpenv4(origin_port)

# The header size limits are raised so that the larger cases are allowed.
ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.response_header_max_size': 1024 * KB,
        'proxy.config.http.header_field_max_size': 65535,
        'proxy.config.http2.max_header_list_size': 1024 * KB,
        'proxy.config.http2.max_continuation_frames_per_minute': 1000,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|http2',
    })
ts.Disk.remap_config.AddLine(f'map http://h2-origin.test/ https://127.0.0.1:{origin_port}/')

for index, (size, status, has_body) in enumerate(CASES):
    path = f"/{size}/{status}" + ("/body" if has_body else "")
    body = "with a body" if has_body else "no body"
    tr = Test.AddTestRun(f"{size} byte header block, status {status}, {body}")
    if index == 0:
        tr.Processes.Default.StartBefore(origin)
        tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.Command = f"{sys.executable} {ORIGIN} check 127.0.0.1 {ts.Variables.port} {path}"
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
        "PASS", "The client should get the origin's status and every header.")
    tr.StillRunningAfter = origin
    tr.StillRunningAfter = ts
