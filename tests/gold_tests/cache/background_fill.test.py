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

Test.Summary = 'Exercise Background Fill'
Test.SkipUnless(Condition.HasCurlFeature('http2'),)
Test.ContinueOnFail = True


class BackgroundFillTest:
    """
    https://docs.trafficserver.apache.org/en/latest/admin-guide/files/records.config.en.html#proxy-config-http-background-fill-completed-threshold
    """

    class State(Enum):
        """
        State of process
        """
        INIT = 0
        RUNNING = 1

    def __init__(self):
        self.state = self.State.INIT
        self.__setupOriginServer()
        self.__setupTS()

    def __setupOriginServer(self):
        self.httpbin = Test.MakeHttpBinServer("httpbin")

    def __setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=True)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self.ts.Disk.records_config.update(
            {
                "proxy.config.http.server_ports": f"{self.ts.Variables.port} {self.ts.Variables.ssl_port}:ssl",
                'proxy.config.ssl.server.cert.path': f"{self.ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self.ts.Variables.SSLDir}",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.background_fill_active_timeout": "0",
                "proxy.config.http.background_fill_completed_threshold": "0.0",
                "proxy.config.http.cache.required_headers": 0,  # Force cache
                "proxy.config.http.insert_response_via_str": 2,
            })

        self.ts.Disk.remap_config.AddLines([
            f"map / http://127.0.0.1:{self.httpbin.Variables.Port}/",
        ])

    def __checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.httpbin
            tr.StillRunningBefore = self.ts
        else:
            tr.Processes.Default.StartBefore(self.httpbin, ready=When.PortOpen(self.httpbin.Variables.Port))
            tr.Processes.Default.StartBefore(self.ts)
            self.state = self.State.RUNNING

    def __checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def __testCase0(self):
        """
        HTTP/1.1 over TCP
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http1.1 -vs http://127.0.0.1:{self.ts.Variables.port}/drip?duration=4;
timeout 2 curl --http1.1 -vs http://127.0.0.1:{self.ts.Variables.port}/drip?duration=4;
sleep 4;
curl --http1.1 -vs http://127.0.0.1:{self.ts.Variables.port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/background_fill_0_stderr.gold"
        self.__checkProcessAfter(tr)

    def __testCase1(self):
        """
        HTTP/1.1 over TLS
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http1.1 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4;
timeout 2 curl --http1.1 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4;
sleep 4;
curl --http1.1 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/background_fill_1_stderr.gold"
        self.__checkProcessAfter(tr)

    def __testCase2(self):
        """
        HTTP/2 over TLS
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http2 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4;
timeout 2 curl --http2 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4;
sleep 4;
curl --http2 -vsk https://127.0.0.1:{self.ts.Variables.ssl_port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/background_fill_2_stderr.gold"
        self.__checkProcessAfter(tr)

    def run(self):
        self.__testCase0()
        self.__testCase1()
        self.__testCase2()


BackgroundFillTest().run()
