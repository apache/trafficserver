'''
webp_transform must refuse an over-cap chunked response
rather than forward an oversized, mislabeled body.

When the origin advertises a Content-Length over the cap, webp_transform
declines the transform up front and the original response passes through. A
chunked response has no Content-Length, so that early check cannot fire and the
per-transaction cap inside consume() is the only bound. The transform produces
nothing until handleInputComplete, so when the cap is exceeded it drops the
buffer, produces no body, and rewrites the status to 502 in the
send-response-headers hook. The client gets a 502 with an empty body rather
than the oversized image. (Transaction::error() cannot be used; it asserts once
the response is in flight, so the status is changed at send time instead.)

This test drives a 20 MiB image/jpeg framed with Transfer-Encoding: chunked
through the plugin and confirms the cap trips mid-stream, ATS does not buffer
the whole body or crash, and the client receives a 502 with a zero-length body
rather than the full 20 MiB image.
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

Test.Summary = 'webp_transform returns 502 for an over-cap chunked no-Content-Length response'

Test.SkipUnless(Condition.PluginExists('webp_transform.so'))

Test.ContinueOnFail = True

server = Test.MakeVerifierServerProcess("server", "replay/webp_chunked_cap.replay.yaml")

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'webp_transform',
})
ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_webp')
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.http_port))

# No Content-Length, so the up-front decline cannot fire; the cap trips inside
# consume(), which emits this message at ERROR level to diags.log. Asserting it
# both confirms the in-transform cap engaged and tells autest the ERROR line is
# expected (the default check fails on any ERROR in diags.log).
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "response body exceeds cap", "The in-transform cap must trip for a chunked body with no Content-Length")

tr = Test.AddTestRun("over-cap chunked body is refused with a 502 and no body")
# -w reports the downloaded body size; braces are doubled so str.format leaves
# the curl %{...} variable intact.
tr.MakeCurlCommand(
    '-sS -D - -o /dev/null -w "size_download=%{{size_download}}" -x 127.0.0.1:{0} '
    '-H "Accept: image/webp" -H "uuid: chunked-huge" http://127.0.0.1:{1}/chunked-huge.jpg'.format(
        ts.Variables.port, server.Variables.http_port),
    ts=ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
# The client gets a 502 with a zero-length body: the oversized image is refused,
# not forwarded. A full response would download 20 MiB.
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 502", "Over-cap image must be refused with a 502")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "size_download=0", "Body must be empty; the full 20 MiB image must not be forwarded")
