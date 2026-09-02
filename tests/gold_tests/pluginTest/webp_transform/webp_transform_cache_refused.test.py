'''
An over-cap response that webp_transform refuses with a 502 must not poison the
cache.

The refused path produces a zero-length body and rewrites the client status to
502 in the send-response-headers hook. The cacheable object, however, is driven
by the origin's 200 server response plus the transform's (empty) output, and the
read hook already stamped a transformed Content-Type onto that server response.
If nothing marks the refused response uncacheable, ATS can store a 200 with an
empty body labeled image/webp and serve that poisoned entry to later clients
while the original requester saw a 502.

This test enables caching (and forces caching of responses without explicit
freshness headers), drives the over-cap chunked body twice through the same URL,
and asserts BOTH requests are refused with a 502 and an empty body. A cached
poisoned 200 would show up as a 200 (and/or a non-zero body) on the second
request.
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

Test.Summary = 'An over-cap refused (502) response must not poison the cache'

Test.SkipUnless(Condition.PluginExists('webp_transform.so'))

Test.ContinueOnFail = True

server = Test.MakeVerifierServerProcess("server", "replay/webp_chunked_cap.replay.yaml")

ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'webp_transform',
        # Cache aggressively: store 200s even without explicit freshness headers,
        # so a refused response that is not marked no-store WOULD be cached. This
        # makes the poisoning observable if the fix regresses.
        'proxy.config.http.cache.required_headers': 0,
        'proxy.config.http.cache.ignore_client_cc_max_age': 1,
    })
ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_webp')
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.http_port))

ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "response body exceeds cap", "The in-transform cap must trip for the chunked body with no Content-Length")

# Request 1: origin is contacted, the cap trips mid-stream, client gets a 502.
tr = Test.AddTestRun("first request: over-cap chunked body refused with 502")
tr.MakeCurlCommand(
    '-sS -D - -o /dev/null -w "size_download=%{{size_download}}" -x 127.0.0.1:{0} '
    '-H "Accept: image/webp" -H "uuid: chunked-huge" http://127.0.0.1:{1}/chunked-huge.jpg'.format(
        ts.Variables.port, server.Variables.http_port),
    ts=ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 502", "First over-cap request must be refused with a 502")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "size_download=0", "First refused response must have an empty body")

# Request 2: if the refused response was cached, this hit would serve a poisoned
# 200 with an empty (or mislabeled) body. The fix marks the refused response
# uncacheable, so this must also be a fresh 502 with an empty body.
tr2 = Test.AddTestRun("second request: must not be served a cached poisoned 200")
tr2.MakeCurlCommand(
    '-sS -D - -o /dev/null -w "size_download=%{{size_download}}" -x 127.0.0.1:{0} '
    '-H "Accept: image/webp" -H "uuid: chunked-huge" http://127.0.0.1:{1}/chunked-huge.jpg'.format(
        ts.Variables.port, server.Variables.http_port),
    ts=ts)
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 502", "Second request must also be a 502, not a cached poisoned 200")
tr2.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "HTTP/1.1 200", "Second request must not be served a cached 200 (cache poisoning)")
tr2.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "size_download=0", "Second response body must be empty; no cached oversized/empty image")
