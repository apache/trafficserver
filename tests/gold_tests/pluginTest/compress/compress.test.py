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

Test.Summary = '''
Test compress plugin
'''

# This test case is very bare-bones.  It only covers a few scenarios that have caused problems.

# Skip if plugins not present.
#
Test.SkipUnless(
    Condition.PluginExists('compress.so'),
    Condition.PluginExists('conf_remap.so'),
    Condition.HasATSFeature('TS_HAS_BROTLI')
)

server = Test.MakeOriginServer("server", options={'--load': '{}/compress_observer.py'.format(Test.TestDirectory)})


def repeat(str, count):
    result = ""
    while count > 0:
        result += str
        count -= 1
    return result


# Need a fairly big body, otherwise the plugin will refuse to compress
body = repeat("lets go surfin now everybodys learnin how\n", 24)
body = body + "lets go surfin now everybodys learnin how"

# expected response from the origin server
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n" +
    'Etag: "359670651"\r\n' +
    "Cache-Control: public, max-age=31536000\r\n" +
    "Accept-Ranges: bytes\r\n" +
    "Content-Type: text/javascript\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}
for i in range(3):
    # add request/response to the server dictionary
    request_header = {
        "headers": "GET /obj{} HTTP/1.1\r\nHost: just.any.thing\r\n\r\n".format(i), "timestamp": "1469733493.993", "body": ""
    }
    server.addResponse("sessionfile.log", request_header, response_header)


# post for the origin server
post_request_header = {
    "headers": "POST /obj3 HTTP/1.1\r\nHost: just.any.thing\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "knock knock"}
server.addResponse("sessionfile.log", post_request_header, response_header)


def curl(ts, idx, encodingList):
    return (
        "curl --verbose --proxy http://127.0.0.1:{}".format(ts.Variables.port) +
        " --header 'X-Ats-Compress-Test: {}/{}'".format(idx, encodingList) +
        " --header 'Accept-Encoding: {0}' 'http://ae-{1}/obj{1}'".format(encodingList, idx) +
        " 2>> compress_long.log ; printf '\n===\n' >> compress_long.log"
    )


def curl_post(ts, idx, encodingList):
    return (
        "curl --verbose -d 'knock knock' --proxy http://127.0.0.1:{}".format(ts.Variables.port) +
        " --header 'X-Ats-Compress-Test: {}/{}'".format(idx, encodingList) +
        " --header 'Accept-Encoding: {0}' 'http://ae-{1}/obj{1}'".format(encodingList, idx) +
        " 2>> compress_long.log ; printf '\n===\n' >> compress_long.log"
    )


waitForServer = True

waitForTs = True

ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'compress',
    'proxy.config.http.normalize_ae': 0,
})

ts.Setup.Copy("compress.config")
ts.Setup.Copy("compress2.config")

ts.Disk.remap_config.AddLine(
    'map http://ae-0/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=compress.so @pparam={}/compress.config'.format(Test.RunDirectory)
)
ts.Disk.remap_config.AddLine(
    'map http://ae-1/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=1' +
    ' @plugin=compress.so @pparam={}/compress.config'.format(Test.RunDirectory)
)
ts.Disk.remap_config.AddLine(
    'map http://ae-2/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=2' +
    ' @plugin=compress.so @pparam={}/compress2.config'.format(Test.RunDirectory)
)
ts.Disk.remap_config.AddLine(
    'map http://ae-3/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=compress.so @pparam={}/compress.config'.format(Test.RunDirectory)
)

for i in range(3):

    tr = Test.AddTestRun()
    if (waitForTs):
        tr.Processes.Default.StartBefore(ts)
    waitForTs = False
    if (waitForServer):
        tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
    waitForServer = False
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Command = curl(ts, i, 'gzip, deflate, sdch, br')

    tr = Test.AddTestRun()
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Command = curl(ts, i, "gzip")

    tr = Test.AddTestRun()
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Command = curl(ts, i, "br")

    tr = Test.AddTestRun()
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Command = curl(ts, i, "deflate")

# Test Aceept-Encoding normalization.

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip;q=0.666")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip;q=0.666x")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip;q=#0.666")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip; Q = 0.666")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip;q=0.0")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "gzip;q=-0.1")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "aaa, gzip;q=0.666, bbb")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, " br ; q=0.666, bbb")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl(ts, 0, "aaa, gzip;q=0.666 , ")

# post
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = curl_post(ts, 3, "gzip")

# compress_long.log contains all the output from the curl commands.  The tr removes the carriage returns for easier
# readability.  Curl seems to have a bug, where it will neglect to output an end of line before outputting an HTTP
# message header line.  The sed command is a work-around for this problem.  greplog.sh uses the grep command to
# select HTTP request/response line that should be consistent every time the test runs.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    r"tr -d '\r' < compress_long.log | sed 's/\(..*\)\([<>]\)/\1\n\2/' | {0}/greplog.sh > compress_short.log"
).format(Test.TestDirectory)
f = tr.Disk.File("compress_short.log")
f.Content = "compress.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo"
f = tr.Disk.File("compress_userver.log")
f.Content = "compress_userver.gold"
