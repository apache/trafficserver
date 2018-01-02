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


class SlowPostAttack:
    def __init__(cls):
        Test.Summary = 'Test how ATS handles the slow-post attack'
        cls._origin_max_connections = 3
        cls._slow_post_client = 'slow_post_client.py'
        cls.setupOriginServer()
        cls.setupTS()
        cls._ts.Setup.CopyAs(cls._slow_post_client, Test.RunDirectory)

    def setupOriginServer(self):
        self._server = Test.MakeOriginServer("server")
        request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
                           "timestamp": "1469733493.993", "body": ""}
        self._server.addResponse("sessionlog.json", request_header, response_header)
        request_header2 = {"headers": "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nHost: www.example.com\r\nConnection: keep-alive\r\n\r\n",
                           "timestamp": "1469733493.993", "body": "a\r\na\r\na\r\n\r\n"}
        response_header2 = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
                            "timestamp": "1469733493.993", "body": ""}
        self._server.addResponse("sessionlog.json", request_header2, response_header2)

    def setupTS(self):
        self._ts = Test.MakeATSProcess("ts", select_ports=False)
        self._ts.Disk.remap_config.AddLine(
            'map / http://127.0.0.1:{0}'.format(self._server.Variables.Port)
        )
        # This plugin can enable request buffer for POST.
        self._ts.Disk.plugin_config.AddLine(
            'request_buffer.so'
        )
        Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir, 'plugins', 'request_buffer.c'), self._ts)
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http',
            'proxy.config.http.origin_max_connections': self._origin_max_connections,
            # Disable queueing when connection reaches limit
            'proxy.config.http.origin_max_connections_queue': 0,
        })

    def run(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = 'python3 {0} -p {1} -c {2}'.format(
            self._slow_post_client, self._ts.Variables.port, self._origin_max_connections)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(Test.Processes.ts)
        tr.Processes.Default.Streams.stdout = "gold/200.gold"


Test.Summary = 'Test how ATS handles the slow-post attack'
slowPostAttack = SlowPostAttack()
slowPostAttack.run()
