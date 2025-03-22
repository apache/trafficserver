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
import os
import re

Test.Summary = 'Exercise HTTP CONNECT Method'
Test.ContinueOnFail = True


class ConnectTest:

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
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.server_ports': f"{self.ts.Variables.port}",
                'proxy.config.http.connect_ports': f"{self.httpbin.Variables.Port}",
            })

        self.ts.Disk.remap_config.AddLines([
            f"map http://foo.com/ http://127.0.0.1:{self.httpbin.Variables.Port}/",
        ])

        self.ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: common
      format: '%<chi> - %<caun> [%<cqtn>] "%<cqhm> %<pqu> %<cqpv>" %<pssc> %<pscl>'
  logs:
    - filename: access
      format: common
'''.split("\n"))

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
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.CurlCommand(f"-v --fail -s -p -x 127.0.0.1:{self.ts.Variables.port} 'http://foo.com/get'")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/connect_0_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testAccessLog(self):
        """Wait for log file to appear, then wait one extra second to make sure TS is done writing it."""
        Test.Disk.File(os.path.join(self.ts.Variables.LOGDIR, 'access.log'), exists=True, content='gold/connect_access.gold')

        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self.ts.Variables.LOGDIR, 'access.log'))
        tr.Processes.Default.ReturnCode = 0

    def run(self):
        self.__testCase0()
        self.__testAccessLog()


ConnectTest().run()


class ConnectViaPVTest:
    # This test also executes the CONNECT request but using proxy verifier to
    # generate traffic
    connectReplayFile = "replays/connect.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("connect-verifier-server", self.connectReplayFile)
        # Verify server output
        self.server.Streams.stdout += Testers.ExcludesExpression("uuid: 1", "Verify the CONNECT request doesn't reach the server.")
        self.server.Streams.stdout += Testers.ContainsExpression(
            "GET /get HTTP/1.1\nuuid: 2", reflags=re.MULTILINE, description="Verify the server gets the second request.")

    def setupTS(self):
        self.ts = Test.MakeATSProcess("connect-ts")

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|iocore_net|rec',
                'proxy.config.http.server_ports': f"{self.ts.Variables.port}",
                'proxy.config.http.connect_ports': f"{self.server.Variables.http_port}",
            })

        self.ts.Disk.remap_config.AddLines([
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/",
        ])
        # Verify ts logs
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f"Proxy's Request.*\n.*\nCONNECT 127.0.0.1:{self.server.Variables.http_port} HTTP/1.1",
            reflags=re.MULTILINE,
            description="Verify that ATS recognizes the CONNECT request.")

    def runTraffic(self):
        tr = Test.AddTestRun("Verify correct handling of CONNECT request")
        tr.AddVerifierClientProcess(
            "connect-client", self.connectReplayFile, http_ports=[self.ts.Variables.port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def __testMetrics(self):
        tr = Test.AddTestRun("Test metrics")
        tr.Processes.Default.Command = (
            f"{Test.Variables.AtsTestToolsDir}/stdout_wait" + " 'traffic_ctl metric get" +
            " proxy.process.http.total_incoming_connections" + " proxy.process.http.total_client_connections" +
            " proxy.process.http.total_client_connections_ipv4" + " proxy.process.http.total_client_connections_ipv6" +
            " proxy.process.http.total_server_connections" + " proxy.process.http2.total_client_connections" +
            " proxy.process.http.connect_requests" + " proxy.process.tunnel.total_client_connections_blind_tcp" +
            " proxy.process.tunnel.current_client_connections_blind_tcp" +
            " proxy.process.tunnel.total_server_connections_blind_tcp" +
            " proxy.process.tunnel.current_server_connections_blind_tcp" +
            " proxy.process.tunnel.total_client_connections_tls_tunnel" +
            " proxy.process.tunnel.current_client_connections_tls_tunnel" +
            " proxy.process.tunnel.total_client_connections_tls_forward" +
            " proxy.process.tunnel.current_client_connections_tls_forward" +
            " proxy.process.tunnel.total_client_connections_tls_partial_blind" +
            " proxy.process.tunnel.current_client_connections_tls_partial_blind" +
            " proxy.process.tunnel.total_client_connections_tls_http" +
            " proxy.process.tunnel.current_client_connections_tls_http" + " proxy.process.tunnel.total_server_connections_tls" +
            " proxy.process.tunnel.current_server_connections_tls'" + f" {Test.TestDirectory}/gold/metrics.gold")
        # Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()
        self.__testMetrics()


ConnectViaPVTest().run()


class ConnectViaPVTest2:
    # This test executes a HTTP/2 CONNECT request with Proxy Verifier.
    connectReplayFile = "replays/connect_h2.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("connect-verifier-server2", self.connectReplayFile)
        # Verify server output
        self.server.Streams.stdout += Testers.ExcludesExpression(
            "test: connect-request", "Verify the CONNECT request doesn't reach the server.")
        self.server.Streams.stdout += Testers.ContainsExpression(
            "GET /get HTTP/1.1\nuuid: 1\ntest: real-request",
            reflags=re.MULTILINE,
            description="Verify the server gets the second(tunneled) request.")

    def setupTS(self):
        self.ts = Test.MakeATSProcess("connect-ts2", enable_tls=True)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|hpack',
                'proxy.config.ssl.server.cert.path': f'{self.ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{self.ts.Variables.SSLDir}',
                'proxy.config.http.server_ports': f"{self.ts.Variables.ssl_port}:ssl",
                'proxy.config.http.connect_ports': f"{self.server.Variables.http_port}",
            })

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        self.ts.Disk.remap_config.AddLines([
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/",
        ])
        # Verify ts logs
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f"Proxy's Request.*\n.*\nCONNECT 127.0.0.1:{self.server.Variables.http_port} HTTP/1.1",
            reflags=re.MULTILINE,
            description="Verify that ATS recognizes the CONNECT request.")

    def runTraffic(self):
        tr = Test.AddTestRun("Verify correct handling of CONNECT request on HTTP/2")
        tr.AddVerifierClientProcess(
            "connect-client2", self.connectReplayFile, https_ports=[self.ts.Variables.ssl_port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


ConnectViaPVTest2().run()
