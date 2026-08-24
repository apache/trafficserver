'''
The max_buffer_size plugin argument overrides the default 16 MiB buffer cap, and
malformed values are rejected so a bad config cannot silently disable the cap.

Two ATS instances:
  - one loads webp_transform with max_buffer_size=1M and is driven with a 2 MiB
    Content-Length image, proving the override is parsed (K/M/G suffix) and
    applied: the transform is declined at the 1 MiB cap, not the 16 MiB default.
  - one loads webp_transform with several malformed max_buffer_size values
    (negative, bad suffix, suffix-multiply overflow) and asserts each is rejected
    with "keeping default 16777216", proving a bad value falls back to the safe
    default rather than wrapping to a huge or unintended cap.
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

Test.Summary = 'max_buffer_size overrides the cap and rejects malformed values'

Test.SkipUnless(Condition.PluginExists('webp_transform.so'))

Test.ContinueOnFail = True

# ---- Instance 1: a valid 1M override takes effect ----
server = Test.MakeOriginServer("server")

# 2 MiB image/jpeg: over the 1 MiB override, well under the 16 MiB default. With
# the override in effect the up-front decline fires at 1 MiB; with the default it
# would not.
TWO_MIB = 2 * 1024 * 1024
server.addResponse(
    "sessionlog.json", {
        "headers": "GET /two_mib.jpg HTTP/1.1\r\nHost: *\r\n\r\n",
        "timestamp": "1",
        "body": ""
    }, {
        "headers":
            "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {0}\r\nConnection: close\r\n\r\n".format(TWO_MIB),
        "timestamp": "1",
        "body": "A" * TWO_MIB
    })

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'webp_transform',
})
ts.Disk.plugin_config.AddLine('webp_transform.so convert_to_webp max_buffer_size=1M')
ts.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

# 1M == 1048576. Seeing the decline name 1048576 (not the 16777216 default)
# proves parse_size parsed the M suffix and the override is what is enforced.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    "exceeds cap 1048576", "max_buffer_size=1M must override the default and decline at 1 MiB")

tr = Test.AddTestRun("2 MiB body declined at the 1 MiB override")
tr.MakeCurlCommand(
    '-sS -D - -o /dev/null -x 127.0.0.1:{0} -H "Accept: image/webp" http://127.0.0.1:{1}/two_mib.jpg'.format(
        ts.Variables.port, server.Variables.Port),
    ts=ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("HTTP/1.1 200", "Declined response is a 200 passthrough")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "[Cc]ontent-[Tt]ype: image/jpeg", "Declined response keeps its original image/jpeg type")

# ---- Instance 2: malformed values are rejected and keep the default ----
ts_bad = Test.MakeATSProcess("ts_bad", enable_cache=False)
ts_bad.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'webp_transform',
})
# Negative (sign guard), bad suffix, and a value that fits in u64 but overflows
# size_t when multiplied by the G suffix (multiply-overflow guard). All must be
# rejected; none may install a cap, so the 16 MiB default must survive.
ts_bad.Disk.plugin_config.AddLine(
    'webp_transform.so convert_to_webp max_buffer_size=-1 max_buffer_size=8X max_buffer_size=20000000000G')
ts_bad.Disk.remap_config.AddLine('map http://127.0.0.1:{0}/ http://127.0.0.1:{0}/'.format(server.Variables.Port))

# TSError() writes these to diags.log at ERROR level. Asserting them here both
# confirms parse_size rejected each value and tells autest the ERROR lines are
# expected (the default check fails on any ERROR in diags.log).
ts_bad.Disk.diags_log.Content = Testers.ContainsExpression(
    "invalid max_buffer_size=-1, keeping default 16777216", "Negative value must be rejected")
ts_bad.Disk.diags_log.Content += Testers.ContainsExpression(
    "invalid max_buffer_size=8X, keeping default 16777216", "Bad suffix must be rejected")
ts_bad.Disk.diags_log.Content += Testers.ContainsExpression(
    "invalid max_buffer_size=20000000000G, keeping default 16777216", "Suffix-multiply overflow must be rejected")

tr_bad = Test.AddTestRun("malformed max_buffer_size values are rejected, default retained")
tr_bad.MakeCurlCommand(
    '-sS -o /dev/null -x 127.0.0.1:{0} http://127.0.0.1:{1}/two_mib.jpg'.format(ts_bad.Variables.port, server.Variables.Port),
    ts=ts_bad)
tr_bad.Processes.Default.StartBefore(ts_bad)
tr_bad.Processes.Default.ReturnCode = 0
