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
Test.SkipUnless(Condition.HasCurlFeature('http2'), Condition.HasProxyVerifierVersion('2.8.0'))
Test.ContinueOnFail = True


class BackgroundFillTest:
    """
    https://docs.trafficserver.apache.org/en/latest/admin-guide/files/records.yaml.en.html#proxy-config-http-background-fill-completed-threshold
    """

    class State(Enum):
        """
        State of process
        """
        INIT = 0
        RUNNING = 1

    def __init__(self):
        self.state = self.State.INIT
        self.ts = {}
        self.__setupOriginServer()
        self.__setupTS(['for_httpbin', 'for_pv'])

    def __setupOriginServer(self):
        self.httpbin = Test.MakeHttpBinServer("httpbin")
        self.pv_server = Test.MakeVerifierServerProcess("server0", "replay/bg_fill.yaml")

    def __setupTS(self, ts_names=['default']):
        for name in ts_names:
            self.ts[name] = Test.MakeATSProcess(name, select_ports=True, enable_tls=True, enable_cache=True)

            self.ts[name].addDefaultSSLFiles()
            self.ts[name].Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

            self.ts[name].Disk.records_config.update(
                {
                    "proxy.config.http.server_ports": f"{self.ts[name].Variables.port} {self.ts[name].Variables.ssl_port}:ssl",
                    "proxy.config.http.background_fill_active_timeout": "0",
                    "proxy.config.http.background_fill_completed_threshold": "0.0",
                    "proxy.config.http.cache.required_headers": 0,  # Force cache
                    "proxy.config.http.insert_response_via_str": 2,
                    'proxy.config.http.server_session_sharing.pool': 'thread',
                    'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
                    'proxy.config.exec_thread.autoconfig.enabled': 0,
                    'proxy.config.exec_thread.limit': 1,
                    'proxy.config.ssl.server.cert.path': f"{self.ts[name].Variables.SSLDir}",
                    'proxy.config.ssl.server.private_key.path': f"{self.ts[name].Variables.SSLDir}",
                    'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
                    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                    "proxy.config.diags.debug.enabled": 3,
                    "proxy.config.diags.debug.tags": "http",
                })

            if name == 'for_httpbin' or name == 'default':
                self.ts[name].Disk.remap_config.AddLines([
                    f"map / http://127.0.0.1:{self.httpbin.Variables.Port}",
                ])
            else:
                self.ts[name].Disk.remap_config.AddLines([
                    f'map / https://127.0.0.1:{self.pv_server.Variables.https_port}',
                ])

    def __checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.httpbin
            tr.StillRunningBefore = self.pv_server
            tr.StillRunningBefore = self.ts['for_httpbin']
            tr.StillRunningBefore = self.ts['for_pv']
        else:
            tr.Processes.Default.StartBefore(self.httpbin, ready=When.PortOpen(self.httpbin.Variables.Port))
            tr.Processes.Default.StartBefore(self.pv_server)
            tr.Processes.Default.StartBefore(self.ts['for_httpbin'])
            tr.Processes.Default.StartBefore(self.ts['for_pv'])
            self.state = self.State.RUNNING

    def __checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.pv_server
        tr.StillRunningAfter = self.ts['for_httpbin']
        tr.StillRunningAfter = self.ts['for_pv']

    def __testCase0(self):
        """
        HTTP/1.1 over TCP
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http1.1 -vs http://127.0.0.1:{self.ts['for_httpbin'].Variables.port}/drip?duration=4;
timeout 2 curl --http1.1 -vs http://127.0.0.1:{self.ts['for_httpbin'].Variables.port}/drip?duration=4;
sleep 4;
curl --http1.1 -vs http://127.0.0.1:{self.ts['for_httpbin'].Variables.port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.Any(
            "gold/background_fill_0_stderr_H.gold", "gold/background_fill_0_stderr_W.gold")
        self.__checkProcessAfter(tr)

    def __testCase1(self):
        """
        HTTP/1.1 over TLS
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http1.1 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4;
timeout 3 curl --http1.1 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4;
sleep 5;
curl --http1.1 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.Any(
            "gold/background_fill_1_stderr_H.gold", "gold/background_fill_1_stderr_W.gold")
        self.__checkProcessAfter(tr)

    def __testCase2(self):
        """
        HTTP/2 over TLS
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"""
curl -X PURGE --http2 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4;
timeout 3 curl --http2 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4;
sleep 5;
curl --http2 -vsk https://127.0.0.1:{self.ts['for_httpbin'].Variables.ssl_port}/drip?duration=4
"""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.Any(
            "gold/background_fill_2_stderr_H.gold", "gold/background_fill_2_stderr_W.gold")
        self.__checkProcessAfter(tr)

    def __testCase3(self):
        """
        HTTP/2 over TLS using ProxyVerifier
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.AddVerifierClientProcess(
            "pv_client",
            "replay/bg_fill.yaml",
            http_ports=[self.ts['for_pv'].Variables.port],
            https_ports=[self.ts['for_pv'].Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/background_fill_3_stdout.gold"
        self.__checkProcessAfter(tr)

    def run(self):
        self.__testCase0()
        self.__testCase1()
        self.__testCase2()
        self.__testCase3()


BackgroundFillTest().run()
