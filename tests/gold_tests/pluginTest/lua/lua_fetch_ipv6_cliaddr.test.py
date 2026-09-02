'''
Verify ts.fetch handles an IPv6 cliaddr option.
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
Verify ts.fetch parses an IPv6 cliaddr and the inner fetch completes.
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

# Inner sub-request fetched by the Lua post_remap hook.
inner_req = {"headers": "GET /inner.txt HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
inner_resp = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "AAAAA"
}
server.addResponse("sessionfile.log", inner_req, inner_resp)

# Outer request driven by curl below.
outer_req = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
outer_resp = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "outer"}
server.addResponse("sessionfile.log", outer_req, outer_resp)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}/'.format(server.Variables.Port) + ' @plugin=tslua.so @pparam=fetch_ipv6_cliaddr.lua')

ts.Setup.Copy("fetch_ipv6_cliaddr.lua", ts.Variables.CONFIGDIR)

ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'ts_lua'})

tr = Test.AddTestRun("ts.fetch with IPv6 cliaddr")
tr.MakeCurlCommand("-s -D - http://127.0.0.1:{0}/".format(ts.Variables.port), ts=ts)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "Sub-Body-Len: 5", "Inner fetch using IPv6 cliaddr should return the full origin body")
tr.StillRunningAfter = server
