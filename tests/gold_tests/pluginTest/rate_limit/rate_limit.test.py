'''
Test rate_limit plugin: connection limit enforcement, queue drain, and 429 rejection.
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
Test rate_limit plugin: concurrent limit enforcement, queue drain, and independent limiters.
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server", delay=3)
ts = Test.MakeATSProcess("ts")

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /slow HTTP/1.1\r\nHost: limit.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Content-Length: 4\r\n"
                   "Connection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "SLOW"
    })

server.addResponse(
    "sessionlog.json", {
        "headers": "GET /fast HTTP/1.1\r\nHost: limit.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Content-Length: 4\r\n"
                   "Connection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "FAST"
    })

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rate_limit',
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.url_remap.remap_required': 1,
    })

# Rule 1: limit=1, no queue — immediate rejection
ts.Disk.remap_config.AddLine(
    f'map http://limit.example.com/ http://127.0.0.1:{server.Variables.Port}/'
    f' @plugin=rate_limit.so @pparam=--limit @pparam=1 @pparam=--queue @pparam=0'
    f' @pparam=--error @pparam=429 @pparam=--retry @pparam=1')

# Rule 2: limit=1, queue=5 — queues excess, resumes when slot freed
ts.Disk.remap_config.AddLine(
    f'map http://queued.example.com/ http://127.0.0.1:{server.Variables.Port}/'
    f' @plugin=rate_limit.so @pparam=--limit @pparam=1 @pparam=--queue @pparam=5'
    f' @pparam=--maxage @pparam=10000 @pparam=--error @pparam=429')

# Rules 3 & 4: two independent limiters (limit=1 each)
ts.Disk.remap_config.AddLine(
    f'map http://limit-a.example.com/ http://127.0.0.1:{server.Variables.Port}/'
    f' @plugin=rate_limit.so @pparam=--limit @pparam=1 @pparam=--queue @pparam=0'
    f' @pparam=--error @pparam=429')

ts.Disk.remap_config.AddLine(
    f'map http://limit-b.example.com/ http://127.0.0.1:{server.Variables.Port}/'
    f' @plugin=rate_limit.so @pparam=--limit @pparam=1 @pparam=--queue @pparam=0'
    f' @pparam=--error @pparam=429')

# Test 1: Concurrent rejection — second request gets 429
tr = Test.AddTestRun("Concurrent requests: second gets 429")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f'sh {Test.TestDirectory}/concurrent_reject.sh {ts.Variables.port}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "fast=429", "Second concurrent request should be rejected with 429")

# Test 2: Sequential requests both pass
tr2 = Test.AddTestRun("Sequential requests: both get 200")
tr2.Processes.Default.Command = f'sh {Test.TestDirectory}/sequential_pass.sh {ts.Variables.port}'
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("first=200", "First sequential request should pass")
tr2.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression(
    "second=200", "Second sequential request should also pass")

# Test 3: Retry-After header on 429 rejection
tr3 = Test.AddTestRun("429 response includes Retry-After header")
tr3.Processes.Default.Command = f'sh {Test.TestDirectory}/retry_after.sh {ts.Variables.port}'
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "Retry-After: 1", "429 response should include Retry-After header")

# Test 4: Queue drain — exercises the fixed reserve() loop
tr4 = Test.AddTestRun("Queue drain: queued request resumes with 200")
tr4.Processes.Default.Command = f'sh {Test.TestDirectory}/queue_drain.sh {ts.Variables.port}'
tr4.Processes.Default.ReturnCode = 0
tr4.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "queued=200", "Queued request should eventually succeed after slot freed")

# Test 5: Independent limiters — saturating one rule doesn't block the other
tr5 = Test.AddTestRun("Independent limiters: rule B passes while rule A is full")
tr5.Processes.Default.Command = f'sh {Test.TestDirectory}/independent_limiters.sh {ts.Variables.port}'
tr5.Processes.Default.ReturnCode = 0
tr5.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "independent=200", "Request to rule B should pass despite rule A being full")

# Test 6: Regression for Finding #106 — queue bypass via incorrect reserve() check.
# With the bug, queued requests are resumed without a valid slot reservation,
# allowing all 3 requests to run concurrently (~3s). With the fix, they serialize
# through the single slot (~9s). We check wall time >= 6s as the pass criterion.
tr6 = Test.AddTestRun("Regression #106: queue does not bypass limit")
tr6.Processes.Default.Command = f'sh {Test.TestDirectory}/queue_bypass_regression.sh {ts.Variables.port}'
tr6.Processes.Default.ReturnCode = 0
tr6.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "timing=correct", "Queued requests must serialize through the limiter, not bypass it")
