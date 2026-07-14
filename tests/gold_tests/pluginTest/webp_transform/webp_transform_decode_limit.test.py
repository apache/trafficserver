'''
A small image that declares dimensions over the ImageMagick decode limits must
not be decoded into a giant pixel buffer; the transform reverts to the original
bytes instead of crashing or exhausting memory.

The byte cap (max_buffer_size) does not help here: a tiny encoded image can sit
well under the cap yet declare enormous dimensions that decode into gigabytes of
pixels. TSPluginInit installs Magick::ResourceLimits (width/height/area/memory/
map and disk(0)) so such an image fails as a caught Magick::Error and the plugin
reverts to the original bytes.

This test serves a valid but over-wide image (16001 px wide, one pixel past the
16000 px width limit) that is only a few hundred KB encoded. ImageMagick sniffs
the body's real format regardless of the declared Content-Type, reads the
over-limit width, and throws; the plugin catches it, logs an ImageMagick error,
and forwards the original bytes. The test asserts ATS does not crash (the client
still gets a 200), the decode-limit error is logged, and the original body is
returned unchanged (not a converted, smaller webp).

A Netpbm P3 (ASCII) image is used so the body is plain text the test origin can
serve verbatim; ImageMagick decodes it identically to a binary image.
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

Test.Summary = 'An over-dimension image is rejected by the decode limits and reverts to the original'

Test.SkipUnless(Condition.PluginExists('webp_transform.so'))

Test.ContinueOnFail = True

# Valid Netpbm P3 (ASCII) image, 16001 px wide by 2 px tall. 16001 > the plugin's
# 16000 px width ResourceLimit, so ImageMagick rejects it on read. Encoded it is
# only a few hundred KB, far under the buffer cap, so the byte cap never fires
# and the decode-side limit is what must catch it.
W, H = 16001, 2
ppm = "P3\n{0} {1}\n255\n".format(W, H) + "".join(("127 127 127 " * W).rstrip() + "\n" for _ in range(H))
PPM_LEN = len(ppm)

server = Test.MakeOriginServer("server")
# Declared image/png so the plugin attempts a png->webp conversion; ImageMagick
# sniffs the actual P3 format from the bytes and reads the over-limit dimensions.
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /overwide.png HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: {0}\r\nConnection: close\r\n\r\n".format(PPM_LEN),
        "timestamp": "1",
        "body": ppm
    })

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'webp_transform',
})
ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_webp')
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

# The decode-limit failure is caught and logged as an ImageMagick error at ERROR
# level. Asserting it confirms the resource limit engaged (not the byte cap) and
# tells autest the ERROR line is expected.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "ImageMagick.. error", "The decode-side ResourceLimit must reject the over-dimension image")

tr = Test.AddTestRun("over-dimension image reverts to original instead of crashing")
tr.MakeCurlCommand(
    '-sS -D - -o /dev/null -w "size_download=%{{size_download}}" -x 127.0.0.1:{0} '
    '-H "Accept: image/webp" http://127.0.0.1:{1}/overwide.png'.format(ts.Variables.port, server.Variables.Port),
    ts=ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
# ATS must stay up and return the original image, not crash and not 502 (the byte
# cap is not exceeded). The reverted body is the original bytes, so its size
# equals the source image, not a smaller converted webp.
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client must get a 200 (decode failed over the limit, original forwarded), not a crash or 502")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "size_download={0}".format(PPM_LEN), "Original bytes must be forwarded unchanged, not a converted webp")
