"""
"""
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

Test.Summary = """
Test range requests
"""

Test.ContinueOnFail = True

CACHE_SHORT_MAXAGE = 1


def register(microserver, request_hdr, request_body, response_hdr, response_body):
    request = {
        "headers": "{}\r\n\r\n".format("\r\n".join(line for line in request_hdr.split("\n") if line)),
        "timestamp": "1469733493.993",
        "body": request_body,
    }
    response = {
        "headers": "{}\r\n\r\n".format("\r\n".join(line for line in response_hdr.split("\n") if line)),
        "timestamp": "1469733493.993",
        "body": response_body,
    }
    microserver.addResponse("sessionlog.json", request, response)


def curl_whole(ts, path=""):
    return f"-sSv -D - http://127.0.0.1:{ts.Variables.port}/{path}"


def curl_range(ts, path="", ifrange=None, start=1, end=5):
    opt = f"-H 'If-Range: {ifrange}'" if ifrange else ""
    return f"-sSv -D - {opt} -H 'Range: bytes={start}-{end}' http://127.0.0.1:{ts.Variables.port}/{path}"


# ----
# Setup Origin Server
# ----
microserver = Test.MakeOriginServer("microserver")

request_hdr = """
GET / HTTP/1.1
Host: 127.0.0.1
"""

response_hdr = """
HTTP/1.1 200 OK
Server: microserver
Connection: close
Cache-Control: max-age=300
Last-Modified: Thu, 10 Feb 2022 00:00:00 GMT
ETag: range
"""

short_cache_request_hdr = """
GET /short HTTP/1.1
Host: 127.0.0.1
"""

short_cache_response_hdr = f"""
HTTP/1.1 200 OK
Server: microserver
Connection: close
Cache-Control: max-age={CACHE_SHORT_MAXAGE}
Last-Modified: Thu, 10 Feb 2022 00:00:00 GMT
ETag: range
"""

response_body = f"{''.join(str(i) for i in range(10))}\n"

register(microserver, request_hdr, "", response_hdr, response_body)
register(microserver, short_cache_request_hdr, "", short_cache_response_hdr, response_body)

# The purpose here is to have a somewhat smarter origin that can respond to If-Modified-Since queries.
# We then can test how the cache server using this origin deals with stale caches.
origin = Test.MakeATSProcess("origin")

origin.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{microserver.Variables.Port}")

origin.Disk.records_config.update(
    {
        "proxy.config.http.cache.http": 1,
        "proxy.config.http.wait_for_cache": 1,
        "proxy.config.http.insert_age_in_response": 0,
        "proxy.config.http.request_via_str": 0,
        "proxy.config.http.response_via_str": 0,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http",
    })

# Make the origin return 304 Not Modified for stale caches
origin.Disk.cache_config.AddLine("dest_ip=127.0.0.1 pin-in-cache=1d")

# ----
# Setup ATS
# ----
# HACK: Don't use a fixed port because it causes ensuing tests to fail. The problem
# appears to be microDNS not allowing address reuse.
#
# https://docs.python.org/3/library/socketserver.html#socketserver.BaseServer.allow_reuse_address
ts = Test.MakeATSProcess("ts")

ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{origin.Variables.port}/")

ts.Disk.records_config.update(
    {
        "proxy.config.http.cache.http": 1,
        "proxy.config.http.cache.range.write": 1,
        "proxy.config.http.response_via_str": 3,
        "proxy.config.http.wait_for_cache": 1,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http",
    })
# ----
# Test Cases
# ----

# On cache miss
# ---

# ATS should ignore the Range header when given an If-Range header with the incorrect etag
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange='"should-not-match"'))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(microserver, ready=When.PortOpen(microserver.Variables.Port))
tr.Processes.Default.StartBefore(origin, ready=When.PortOpen(origin.Variables.port))
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# On cache hit
# ---

# ATS should respond to Range requests with partial content
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-206.gold"

# ATS should respond to Range requests with partial content when given an If-Range header with
# the correct etag
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange='"range"'))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-206.gold"

# ATS should respond to Range requests with partial content when given an If-Range header
# that matches the Last-Modified header of the cached response
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange="Thu, 10 Feb 2022 00:00:00 GMT"))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-206.gold"

# ATS should respond to Range requests with the full content when given an If-Range header
# that doesn't match the Last-Modified header of the cached response
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange="Thu, 10 Feb 2022 01:00:00 GMT"))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# ATS should respond to Range requests with a 416 error code when the given Range is invalid
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, start=100, end=105))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-416.gold"

# ATS should ignore the Range header when given an If-Range header with the incorrect etag
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange='"should-not-match"'))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# ATS should ignore the Range header when given an If-Range header with weak etags
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange='W/"range"'))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# ATS should ignore the Range header when given an If-Range header with a date older than the
# Last-Modified header of the cached response
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_range(ts, ifrange="Wed, 09 Feb 2022 23:00:00 GMT"))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# Write to the cache by requesting the entire content
tr = Test.AddTestRun()
tr.MakeCurlCommand(curl_whole(ts, path="short"))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-200.gold"

# ATS should respond to range requests with partial content for stale caches in response to
# valid If-Range requests if the origin responds with 304 Not Modified.
tr = Test.AddTestRun()
tr.MakeCurlCommandMulti(f"sleep {2 * CACHE_SHORT_MAXAGE}; {{curl}} " + curl_range(ts, path="short", ifrange='"range"'))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/range-206-revalidated.gold"

tr.StillRunningAfter = origin
tr.StillRunningAfter = microserver
tr.StillRunningAfter = ts
