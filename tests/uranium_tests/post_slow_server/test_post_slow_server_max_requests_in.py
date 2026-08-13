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

from uranium_testkit.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_post_slow_server_max_requests_in(urtest: UraniumTest) -> None:
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

    from enum import Enum

    urtest.Summary = 'Exercise POST request with max_requests_in'
    urtest.ContinueOnFail = True

    class PostAndMaxRequestsInTest:
        """
        Cover #8273 - Make sure inbound side inactive timeout doesn't happens during outbound side TLS handshake
        """

        def __init__(self):
            self.__setupOriginServer()
            self.__setupTS()

        def __setupOriginServer(self):
            urtest.GetTcpPort("server_port")
            self.origin_server = urtest.Processes.Process(
                "server", "bash -c '" + urtest.TestDirectory + "/server.sh {}'".format(urtest.Variables.server_port))

        def __setupTS(self):
            self.ts = urtest.MakeATSProcess("ts")

            self.ts.Disk.records_config.update(
                {
                    "proxy.config.http.server_ports": f"{self.ts.Variables.port} {self.ts.Variables.uds_path}",
                    "proxy.config.net.max_requests_in": 1000,
                    'proxy.config.http.connect_attempts_timeout': 1,
                    "proxy.config.diags.debug.enabled": 1,
                    "proxy.config.diags.debug.tags": "http|socket|v_net_queue",
                })

            self.ts.Disk.remap_config.AddLines([
                f"map / https://127.0.0.1:{urtest.Variables.server_port}/",
            ])

        def __testCase0(self):
            """
            - POST request
            - Outbound side TLS Handshake hits connect_attempts_timeout
            - Client gets 502
            """
            tr = urtest.AddTestRun()
            tr.Processes.Default.StartBefore(self.origin_server)
            tr.Processes.Default.StartBefore(self.ts)
            tr.MakeCurlCommand(f"-X POST --http1.1 -vs http://127.0.0.1:{self.ts.Variables.port}/ --data key=value", ts=self.ts)
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.stdout = "gold/post_slow_server_max_requests_in_0_stdout.gold"
            tr.Processes.Default.Streams.stderr = "gold/post_slow_server_max_requests_in_0_stderr.gold"
            tr.StillRunningAfter = self.ts

        def run(self):
            self.__testCase0()

    PostAndMaxRequestsInTest().run()
    urtest.execute()
