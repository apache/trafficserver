'''
Verify ts.remap.* APIs respect their documented do_remap-only context.
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
The ts.remap.* family is documented as context: do_remap. Verify that
calling one (ts.remap.get_from_url_host) from a transaction hook
registered during do_remap returns nil and does not crash, while the
same call inside do_remap returns the from-URL host.
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

req = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
resp = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ok"}
server.addResponse("sessionfile.log", req, resp)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=tslua.so @pparam=remap_after_hook.lua')

ts.Setup.Copy("remap_after_hook.lua", ts.Variables.CONFIGDIR)

ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'ts_lua'})

tr = Test.AddTestRun("ts.remap.* in do_remap and from a txn hook")
tr.MakeCurlCommand("-s -D - -H 'Host: www.example.com' http://127.0.0.1:{0}/".format(ts.Variables.port), ts=ts)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
    "Remap-From-Host: www.example.com", "ts.remap.* should return the from-URL host inside do_remap")
tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression(
    "Hook-From-Host: <nil>", "ts.remap.* should return nil when called outside do_remap")
tr.StillRunningAfter = server
