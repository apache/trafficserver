'''
webp_transform must not buffer unbounded response bodies.

ImageTransform buffered the entire origin response in memory before handing it
to ImageMagick, so a large image response could exhaust proxy
memory, and a decode could throw an exception the narrow catch did not handle.

The fix bounds this two ways: when the origin advertises a Content-Length over
the 16 MiB cap, the transform is declined up front and the original response
passes through untouched and keeps its original Content-Type; bodies without a
usable Content-Length are still bounded by a per-transaction cap inside the
transform.

This test drives a 20 MiB image/jpeg with a Content-Length through ATS with
webp_transform loaded (convert_to_webp) over both HTTP/1.1 and HTTP/2, and
confirms the client gets a 200 with Content-Type image/jpeg (declined, not a
mislabeled image/webp) rather than ATS buffering the body or crashing.
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

Test.Summary = 'webp_transform must not buffer unbounded response bodies'

Test.SkipUnless(
    Condition.PluginExists('webp_transform.so'),
    Condition.HasCurlFeature('http2'),
)

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")

# A 20 MiB image/jpeg with a Content-Length over the 16 MiB cap. ImageMagick
# would reject these bytes, so the point is that the transform is declined up
# front and ATS never buffers or decodes them.
BIG = 20 * 1024 * 1024
body = "A" * BIG
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /huge.jpg HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {0}\r\n\r\n".format(BIG),
        "timestamp": "1",
        "body": body
    })

ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
ts.addDefaultSSLFiles()

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'webp_transform',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_webp')

# Plugin debug goes to traffic.out. Asserting the decline message naming the
# default 16 MiB cap (16777216) proves the plugin actually engaged and declined,
# rather than the response merely passing through untouched.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    "exceeds cap 16777216", "Plugin must engage and decline at the default 16 MiB cap")

# Identity rule for the HTTP/1.1 forward-proxy run, plus a catch-all so the
# HTTP/2 reverse-proxy run resolves to the same origin.
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}/'.format(server.Variables.Port))

# The unbounded buffering is in the origin-response transform, so it is independent of
# the client protocol. Exercise both H1 and H2 to confirm the oversized body is
# declined and the client gets a truthful 200 image/jpeg either way.
tr = Test.AddTestRun("HTTP/1.1 client: oversized body declined, original type preserved")
tr.MakeCurlCommandMulti(
    '{curl} -sS -D - -o /dev/null -x 127.0.0.1:TSPORT -H "Accept: image/webp" http://127.0.0.1:OPORT/huge.jpg'.replace(
        'TSPORT', str(ts.Variables.port)).replace('OPORT', str(server.Variables.Port)),
    ts=ts)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 200", "H1 client must see 200 OK, not a crash")
# The response must keep its truthful image/jpeg type. A mislabeled image/webp
# would mean the original bytes were forwarded under a transformed Content-Type.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "[Cc]ontent-[Tt]ype: image/jpeg", "Declined response must keep its original image/jpeg type")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "image/webp", "Declined response must not be mislabeled image/webp")

tr2 = Test.AddTestRun("HTTP/2 client: oversized body declined, original type preserved")
tr2.MakeCurlCommand(
    '--http2 -k -sS -D - -o /dev/null -H "Accept: image/webp" https://127.0.0.1:{0}/huge.jpg'.format(ts.Variables.ssl_port), ts=ts)
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/2 200", "H2 client must see 200 OK, not a crash")
tr2.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "[Cc]ontent-[Tt]ype: image/jpeg", "Declined response must keep its original image/jpeg type")
tr2.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "image/webp", "Declined response must not be mislabeled image/webp")
