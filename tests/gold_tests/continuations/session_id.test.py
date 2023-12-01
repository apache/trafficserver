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

Test.Summary = '''
Verify session ID properties.
'''

Test.SkipUnless(Condition.HasCurlFeature('http2'))

# Configure the server.
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length:0\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionfile.log", request_header, response_header)

# Configure ATS. Disable the cache to simplify the test.
ts = Test.MakeATSProcess("ts", command="traffic_manager", enable_tls=True, enable_cache=False)

ts.addDefaultSSLFiles()

Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'continuations', 'plugins', '.libs', 'session_id_verify.so'), ts)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'session_id_verify',
        'proxy.config.cache.enable_read_while_writer': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

#
# Run some HTTP/1 traffic.
#
tr = Test.AddTestRun("Perform HTTP/1 transactions")
cmd = 'curl -v -H "host:example.com" http://127.0.0.1:{0}'.format(ts.Variables.port)
numberOfRequests = 100
# Create a bunch of curl commands to be executed in parallel. Default.Process
# is set in SpawnCommands.  On Fedora 28/29, it seems that curl will
# occasionally timeout after a couple seconds and return exitcode 2
# Examining the packet capture shows that Traffic Server dutifully sends the response
ps = tr.SpawnCommands(cmdstr=cmd, count=numberOfRequests, retcode=Any(0, 2))
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = Any(0, 2)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
ts.StartAfter(*ps)
server.StartAfter(*ps)

tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# Run some HTTP/2 traffic.
#
tr = Test.AddTestRun("Perform HTTP/2 transactions")
cmd = 'curl -v -k --http2 -H "host:example.com" https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
ps = tr.SpawnCommands(cmdstr=cmd, count=numberOfRequests, retcode=Any(0, 2))
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = Any(0, 2)

#
# Verify that the session ids are unique.
#

# AuTest already searches for errors in diags.log and fails if it encounters
# them. The test plugin prints an error to this log if it sees duplicate ids.
# The following is to verify that we encountered the expected ids.


def verify_session_count(output):
    global numberOfRequests
    nReq = numberOfRequests * 2
    session_ids = [line[0:line.find("\n")] for line in str(output).split("session id: ")[1:]]
    if len(session_ids) != nReq:
        return "Found {} session_id's, expected {}".format(len(session_ids), nReq)
    return ""


ts.Disk.traffic_out.Content += Testers.FileContentCallback(verify_session_count, 'verify_session_count')
