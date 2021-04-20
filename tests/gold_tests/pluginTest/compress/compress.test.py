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

import os
import subprocess
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

def curl(ts, name, idx, encodingList):
    return (
        "curl --verbose --proxy http://127.0.0.1:{}".format(ts.Variables.port) +
        " --header 'X-Ats-Compress-Test: {}/{}/{}'".format(name, idx, encodingList) +
        " --header 'Accept-Encoding: {0}' 'http://ae-{1}/obj{1}'".format(encodingList, idx) +
        " >> {0}/compress_long.log 2>&1 ; printf '\n\n' >> {0}/compress_long.log".format(Test.RunDirectory)
    )

def oneTs(name, AeHdr1='gzip, deflate, sdch, br'):
    global waitForServer

    waitForTs = True

    ts = Test.MakeATSProcess(name)

    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 0,
        'proxy.config.diags.debug.tags': 'http|compress|cache',
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

    for i in range(3):

        tr = Test.AddTestRun()
        if (waitForTs):
            tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.port))
        waitForTs = False
        if (waitForServer):
            tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
        waitForServer = False
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Command = curl(ts, name, i, AeHdr1)

        tr = Test.AddTestRun()
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Command = curl(ts, name, i, "gzip")

        tr = Test.AddTestRun()
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Command = curl(ts, name, i, "br")

        tr = Test.AddTestRun()
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Command = curl(ts, name, i, "deflate")

waitForServer = True

oneTs("ts")
oneTs("ts2", "gzip")
oneTs("ts3", "deflate")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    r"tr -d '\r' < {1}/compress_long.log | sed 's/\(..*\)\([<>]\)/\1\n\2/' | {0}/greplog.sh > {1}/compress_short.log"
).format(Test.TestDirectory, Test.RunDirectory)
f = tr.Disk.File("compress_short.log")
f.Content = "compress.gold"

# Have to comment this out, because caching does not seem to be consistent, which is disturbing.
#
# tr = Test.AddTestRun()
# tr.Processes.Default.Command = "echo"
# f = tr.Disk.File("compress_userver.log")
# f.Content = "compress_userver.gold"
