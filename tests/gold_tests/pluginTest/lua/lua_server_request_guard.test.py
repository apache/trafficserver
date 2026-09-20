'''
Verify ts.server_request APIs fail safely before a server request exists.
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
The ts.server_request APIs are only backed by a server request after ATS
constructs one. Verify calls made from do_remap return nil and do not crash.
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True


class LuaServerRequestGuardTest:
    """
    Verify ts.server_request guards before ATS creates a server request.
    """

    _headers = (
        "Early-Server-Header",
        "Early-Server-Header-Table",
        "Early-Server-Headers",
        "Early-Server-Uri",
        "Early-Server-Uri-Args",
        "Early-Server-Method",
        "Early-Server-Url-Host",
        "Early-Server-Url-Scheme",
        "Early-Server-Version",
    )

    def __init__(self):
        self._ts = Test.MakeATSProcess("ts")
        self._server = Test.MakeOriginServer("server")
        self._configure_server()
        self._configure_ats()

    def _configure_server(self):
        req = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        resp = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ok"}
        self._server.addResponse("sessionfile.log", req, resp)

    def _configure_ats(self):
        self._ts.Disk.remap_config.AddLine(
            f'map http://www.example.com/ http://127.0.0.1:{self._server.Variables.Port}/'
            ' @plugin=tslua.so @pparam=server_request_guard.lua')

        self._ts.Setup.Copy("server_request_guard.lua", self._ts.Variables.CONFIGDIR)

        self._ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'ts_lua'})

    def run(self):
        tr = Test.AddTestRun("ts.server_request guards before server request creation")
        tr.MakeCurlCommand(f"-s -D - -H 'Host: www.example.com' http://127.0.0.1:{self._ts.Variables.port}/", ts=self._ts)
        tr.Processes.Default.StartBefore(self._server, ready=When.PortOpen(self._server.Variables.Port))
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.ReturnCode = 0

        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
            f"{self._headers[0]}: <nil>", f"{self._headers[0]} should be nil before ATS creates the server request")
        for header in self._headers[1:]:
            tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression(
                f"{header}: <nil>", f"{header} should be nil before ATS creates the server request")

        tr.StillRunningAfter = self._server


LuaServerRequestGuardTest().run()
