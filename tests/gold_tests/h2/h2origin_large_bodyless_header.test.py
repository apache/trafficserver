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
# to the HttpSM as one buffer spanning several IOBuffer blocks, with EOS set.
# Every response here must reach the client with the origin's status and
# headers rather than a 5xx.
Test.ATSReplayTest(replay_file="replay/h2origin_large_bodyless_header.replay.yaml")

# Larger header blocks come from a Python origin, because Proxy Verifier will not
# send a header block over 64 KiB. Each case is (header block size, status, has body).
ORIGIN = os.path.join(Test.TestDirectory, 'h2_large_header_origin.py')
KB = 1024

# Under the default 32 KB header size limits.
DEFAULT_LIMIT_CASES = [(16 * KB, 302, False), (28 * KB, 204, False)]
# With the header block limits raised to 1 MB and the per-field limit to 64 KB.
RAISED_LIMIT_CASES = [
    (64 * KB, 302, False),
    (128 * KB, 204, False),
    (256 * KB, 302, False),
    (512 * KB, 204, False),
    (900 * KB, 302, False),
    (512 * KB, 200, True),
]
# Over the default limits, which must still fail cleanly with a 502.
OVER_DEFAULT_LIMIT_CASES = [(64 * KB, 302, False)]

origin = Test.Processes.Process("h2-large-header-origin")
origin_port = get_port(origin, "origin_port")
ssl_dir = os.path.join(Test.Variables.AtsTestToolsDir, "ssl")
origin.Command = (
    f"{sys.executable} {ORIGIN} serve 127.0.0.1 {origin_port}"
    f" --cert {os.path.join(ssl_dir, 'server.pem')} --key {os.path.join(ssl_dir, 'server.key')}")
origin.Ready = When.PortOpenv4(origin_port)


def make_ts(name: str, records: dict) -> 'Process':
    ts = Test.MakeATSProcess(name, enable_cache=False)
    ts.Disk.records_config.update(
        {
            'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|http2',
        })
    ts.Disk.records_config.update(records)
    ts.Disk.remap_config.AddLine(f'map http://h2-origin.test/ https://127.0.0.1:{origin_port}/')
    return ts


ts_default = make_ts("ts-default", {})
ts_default.Disk.diags_log.Content = Testers.ContainsExpression(
    "continuation compression error", "A header block over the default limits should be rejected.")
ts_raised = make_ts(
    "ts-raised", {
        'proxy.config.http.response_header_max_size': 1024 * KB,
        'proxy.config.http.header_field_max_size': 65535,
        'proxy.config.http2.max_header_list_size': 1024 * KB,
        'proxy.config.http2.max_continuation_frames_per_minute': 1000,
    })


def add_check(ts: 'Process', size: int, status: int, has_body: bool, expect_status: int = 0) -> None:
    path = f"/{size}/{status}" + ("/body" if has_body else "")
    expected = f"a {expect_status}" if expect_status else f"the origin's {status} and headers"
    tr = Test.AddTestRun(f"{size // KB} KB header block, status {status}: client gets {expected}")
    if not add_check.started:
        tr.Processes.Default.StartBefore(origin)
        tr.Processes.Default.StartBefore(ts_default)
        tr.Processes.Default.StartBefore(ts_raised)
        add_check.started = True
    command = f"{sys.executable} {ORIGIN} check 127.0.0.1 {ts.Variables.port} {path}"
    if expect_status:
        command += f" --expect-status {expect_status}"
    tr.Processes.Default.Command = command
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("PASS", "The client should get the expected response.")
    tr.StillRunningAfter = origin
    tr.StillRunningAfter = ts_default
    tr.StillRunningAfter = ts_raised


add_check.started = False
for size, status, has_body in DEFAULT_LIMIT_CASES:
    add_check(ts_default, size, status, has_body)
for size, status, has_body in RAISED_LIMIT_CASES:
    add_check(ts_raised, size, status, has_body)
for size, status, has_body in OVER_DEFAULT_LIMIT_CASES:
    add_check(ts_default, size, status, has_body, expect_status=502)
