'''
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

import sys

from ports import get_port

Test.Summary = '''
Slice plugin: verify no integer underflow when content shrinks below block position.

When origin reports Content-Range with total length smaller than the requested
block position (content shrunk between fetches), the plugin must fail gracefully
rather than underflowing m_blockskip which would corrupt the response.
'''

Test.SkipUnless(Condition.PluginExists('slice.so'),)
Test.ContinueOnFail = False

# Define ATS - no cache so every slice sub-request goes to origin
ts = Test.MakeATSProcess("ts", enable_cache=False)

# Test: Request bytes 14-20 via slice plugin (blockbytes=7)
# Slice will:
#   1. Fetch block 0 (reference): gets CL=21, etag "old"
#   2. Fetch block 2 (interior, skips block 1): gets CL=10, etag "new" (MISMATCH! m_contentlen=10)
#   3. Refetch block 0 (reference): gets CL=10, etag "new" (matches new m_contentlen)
#   4. Enters ActiveRef: blockpos=14, m_contentlen=10 => guard triggers, Fail state
#
# Client should get an error/empty response, NOT corrupted data.

tr = Test.AddTestRun("Request triggering content shrink underflow guard")

# Copy and start custom origin server
tr.Setup.CopyAs("shrink_origin.py")

origin = tr.Processes.Process("origin")
origin_port = get_port(origin, 'http_port')
origin.Command = f'{sys.executable} shrink_origin.py {origin_port}'
origin.Ready = When.PortOpenv4(origin_port)

# Configure remap to point at our custom origin
ts.Disk.remap_config.AddLines(
    [
        f'map http://slice/ http://127.0.0.1:{origin_port}/'
        ' @plugin=slice.so @pparam=--blockbytes-test=7',
    ])

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'slice',
})

ps = tr.Processes.Default
ps.StartBefore(origin)
ps.StartBefore(Test.Processes.ts)
ps.Command = (
    f'curl -s -D /dev/stdout -o /dev/stderr'
    f' -x localhost:{ts.Variables.port}'
    f' http://slice/shrink -r 14-20'
    f' -w "\\nSIZE:%{{size_download}}"')
ps.Streams.stdout = Testers.ContainsExpression(r"SIZE:0\b", "expected zero client-visible body size")
ps.Streams.stderr = Testers.ExcludesExpression(r".", "expected no client-visible response body")
tr.StillRunningAfter = ts

# Test 2: Non-block-aligned range. blockbytes=7, client requests bytes=16-20.
# firstblock=2, blockpos=14. Content shrinks to 15.
# m_contentlen(15) > blockpos(14) would pass a blockpos-only guard, but
# m_contentlen(15) <= m_req_range.m_beg(16) catches it.
tr = Test.AddTestRun("Mid-block range: content shrinks above blockpos but below range start")
ps = tr.Processes.Default
ps.Command = (
    f'curl -s -D /dev/stdout -o /dev/stderr'
    f' -x localhost:{ts.Variables.port}'
    f' http://slice/shrink_mid -r 16-20'
    f' -w "\\nSIZE:%{{size_download}}"')
tr.StillRunningAfter = ts

# Verify the error was logged (our new guard message)
ts.Disk.diags_log.Content = Testers.ContainsExpression("shrunk below requested range start", "expected underflow guard error log")
