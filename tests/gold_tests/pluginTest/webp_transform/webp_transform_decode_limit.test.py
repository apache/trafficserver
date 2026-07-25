'''
A small image that declares dimensions over the ImageMagick decode limits must
not be decoded into a giant pixel buffer; the transform reverts to the original
bytes instead of crashing or exhausting memory.

The byte cap (max_buffer_size) does not help here: a tiny encoded image can sit
well under the cap yet declare enormous dimensions that decode into gigabytes of
pixels. TSPluginInit installs Magick::ResourceLimits (width/height/area/memory/
map and disk(0)) so such an image fails as a caught Magick::Error and the plugin
reverts to the original bytes.

This test serves a minimal, over-wide WebP (VP8L) image: a real RIFF/WEBP/VP8L
signature followed by a bit-packed header declaring 16129x2 (16129 > the
plugin's 16000 px width limit), with no bitstream payload beyond the header.
ImageMagick's decoder reads the declared width/height straight out of that
header and throws before it would ever need pixel data; the plugin catches it,
logs an ImageMagick error, and forwards the original bytes. The test asserts
ATS does not crash (the client still gets a 200), the decode-limit error is
logged, and the original body is returned unchanged (not a converted jpeg).

The plugin's has_signature_for() guard checks the declared encoding's magic
bytes before ImageMagick ever sees the body, so the served body must carry a
real signature -- an arbitrary/mislabeled body would be caught by that guard
instead and never reach the decode-limit code path this test targets. WebP is
used (rather than PNG or JPEG) because it is the only one of the three
signatures the plugin recognizes that can be built entirely from bytes <=0x7f;
the origin server writes the body via a UTF-8 encode, so any byte over 0x7f
would not survive the round trip unchanged.
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

import struct

Test.Summary = 'An over-dimension image is rejected by the decode limits and reverts to the original'

Test.SkipUnless(Condition.PluginExists('webp_transform.so'))

Test.ContinueOnFail = True

# A minimal WebP (VP8L) image, 16129 px wide by 2 px tall. 16129 > the plugin's
# 16000 px width ResourceLimit, so ImageMagick rejects it on read. There is no
# bitstream data beyond the 5-byte VP8L header (signature byte + packed
# width/height), so the body is a few dozen bytes, far under the buffer cap --
# the decode-side limit is what must catch it, not the byte cap. 16129x2 is
# also chosen so every byte of the packed header is <=0x7f (see module
# docstring for why that matters).
W, H = 16129, 2
vp8l_payload = b'\x2f' + struct.pack('<I', (W - 1) | ((H - 1) << 14))
# RIFF chunks pad to an even length; the chunk-size field itself stays unpadded.
padded_payload = vp8l_payload + (b'\x00' if len(vp8l_payload) % 2 else b'')
webp_chunk = b'VP8L' + struct.pack('<I', len(vp8l_payload)) + padded_payload
webp_body_bytes = b'WEBP' + webp_chunk
webp_body_bytes = b'RIFF' + struct.pack('<I', len(webp_body_bytes)) + webp_body_bytes
assert all(b <= 0x7f for b in webp_body_bytes)
webp_body = webp_body_bytes.decode('ascii')
WEBP_LEN = len(webp_body)

server = Test.MakeOriginServer("server")
# Declared image/webp so the plugin attempts a webp->jpeg conversion; the body
# carries a real RIFF/WEBP/VP8L signature so has_signature_for() lets it
# through to ImageMagick, which reads the over-limit dimensions from the VP8L
# header.
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /overwide.webp HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers":
            "HTTP/1.1 200 OK\r\nContent-Type: image/webp\r\nContent-Length: {0}\r\nConnection: close\r\n\r\n".format(WEBP_LEN),
        "timestamp": "1",
        "body": webp_body
    })

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'webp_transform',
})
ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_jpeg')
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

# The decode-limit failure is caught and logged as an ImageMagick error at ERROR
# level. Asserting it confirms the resource limit engaged (not the byte cap) and
# tells autest the ERROR line is expected.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "ImageMagick.. error", "The decode-side ResourceLimit must reject the over-dimension image")

tr = Test.AddTestRun("over-dimension image reverts to original instead of crashing")
tr.MakeCurlCommand(
    '-sS -D - -o /dev/null -w "size_download=%{{size_download}}" -x 127.0.0.1:{0} '
    '-H "Accept: image/jpeg" http://127.0.0.1:{1}/overwide.webp'.format(ts.Variables.port, server.Variables.Port),
    ts=ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
# ATS must stay up and return the original image, not crash and not 502 (the byte
# cap is not exceeded). The reverted body is the original bytes, so its size
# equals the source image, not a smaller converted jpeg.
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client must get a 200 (decode failed over the limit, original forwarded), not a crash or 502")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "size_download={0}".format(WEBP_LEN), "Original bytes must be forwarded unchanged, not a converted jpeg")
