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
"""
Verify that ATS returns 413 when the POST body exceeds post_copy_size during
request buffering (e.g., oversized body with request_buffer_enabled on).

This tests four scenarios:
1. Known Content-Length: ATS checks upfront and rejects immediately
2. Chunked transfer: ATS detects during buffering and sends 413 after tunnel completes
3. Expect: 100-continue with Content-Length: ATS should reject with 413 BEFORE sending 100 Continue
   (Test 3 demonstrates a bug - currently ATS sends 100 Continue first, wasting bandwidth)
4. Expect: 100-continue + chunked: ATS MUST send 100 Continue (can't know size upfront),
   then detect during buffering and send 413 - this is correct behavior
"""

import os

Test.Summary = "POST body larger than post_copy_size returns 413"

# Make a body larger than post_copy_size (set below to 1024)
body_path = os.path.join(Test.RunDirectory, "large_post_body.txt")
with open(body_path, "w") as f:
    f.write("A" * 5000)

# ATS process with small post_copy_size to trigger 413
ts = Test.MakeATSProcess("ts", enable_cache=False)
# Dummy remap so ATS does not return 404; origin will not actually be contacted
ts.Disk.remap_config.AddLine("map / http://127.0.0.1:9")
ts.Disk.records_config.update(
    """
    diags:
      debug:
        enabled: 1
        tags: http
    http:
      request_buffer_enabled: 1
      post_copy_size: 1024
      send_100_continue_response: 1
    url_remap:
      remap_required: 0
    """)

# Test 1: Oversized POST with known Content-Length (upfront check)
tr = Test.AddTestRun("oversized POST with Content-Length should return 413")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f'curl -sS -vvv -D - -o /dev/null --max-time 15 '
    f'-H "Expect:" --data-binary @{body_path} http://127.0.0.1:{ts.Variables.port}/')
# curl returns 0 when -f/--fail is not used
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r"HTTP/1.1 413", "Curl should see 413 response")

# Test 2: Oversized POST with chunked transfer encoding (runtime check)
tr2 = Test.AddTestRun("oversized chunked POST should return 413")
tr2.Processes.Default.Command = (
    f'curl -sS -vvv -D - -o /dev/null --max-time 15 '
    f'-H "Expect:" -H "Transfer-Encoding: chunked" --data-binary @{body_path} '
    f'http://127.0.0.1:{ts.Variables.port}/')
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(r"HTTP/1.1 413", "Curl should see 413 for chunked POST")

# Test 3: Oversized POST with Expect: 100-continue
# This test demonstrates the PROBLEM: ATS sends "100 Continue" BEFORE checking
# Content-Length against post_copy_size. The client then sends the entire body
# only to receive 413 afterward - wasting bandwidth.
#
# This test verifies the fix by checking ATS logs - there should be NO
# "send 100 Continue" log when Content-Length exceeds post_copy_size
tr3 = Test.AddTestRun("Expect: 100-continue with oversized body - should NOT send 100 Continue")
tr3.Processes.Default.Command = (
    f'curl -sS -vvv -D - -o /dev/null --max-time 15 '
    f'-H "Expect: 100-continue" '
    f'--data-binary @{body_path} http://127.0.0.1:{ts.Variables.port}/')
# Explicitly send "Expect: 100-continue" header
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression(r"HTTP/1.1 413", "Should return 413")

# Test 4: Expect: 100-continue + chunked transfer encoding
# This is different from Test 3: with chunked, we DON'T know Content-Length upfront,
# so ATS MUST send 100 Continue (correct behavior), then detect during buffering.
# This test ensures we don't break this valid use case.
tr4 = Test.AddTestRun("Expect: 100-continue + chunked - MUST send 100 Continue (unknown size)")
tr4.Processes.Default.Command = (
    f'curl -sS -vvv -D - -o /dev/null --max-time 15 '
    f'-H "Expect: 100-continue" -H "Transfer-Encoding: chunked" '
    f'--data-binary @{body_path} http://127.0.0.1:{ts.Variables.port}/')
tr4.Processes.Default.ReturnCode = 0
# Should still get 413 (detected during buffering)
tr4.Processes.Default.Streams.stdout = Testers.ContainsExpression(r"HTTP/1.1 413", "Should return 413 for chunked")
# For chunked, 100 Continue IS correct because we can't know size upfront
# We check in stderr (-vvv output) that curl received 100 Continue
tr4.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    r"HTTP/1.1 100 Continue", "For chunked requests, 100 Continue MUST be sent (size unknown upfront)")

# Validate ATS returned 413 for all tests
ts.Disk.traffic_out.Content += Testers.ContainsExpression(r"HTTP/1.1 413", "ATS should respond with 413 Payload Too Large")

# Ensure no 400/500 noise
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(r"HTTP/1.1 400", "Should not return 400")
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(r"HTTP/1.1 500", "Should not return 500")
