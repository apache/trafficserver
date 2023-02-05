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
from ports import get_port
import sys

Test.Summary = 'Test PROXY Protocol'
Test.SkipUnless(
    Condition.HasCurlOption("--haproxy-protocol")
)
Test.ContinueOnFail = True


class ProxyProtocolTest:
    """Test that ATS can receive Proxy Protocol."""

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.httpbin = Test.MakeHttpBinServer("httpbin")
        # TODO: when httpbin 0.8.0 or later is released, remove below json pretty print hack
        self.json_printer = f'''
{sys.executable} -c "import sys,json; print(json.dumps(json.load(sys.stdin), indent=2, separators=(',', ': ')))"
'''

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts_in", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.httpbin.Variables.Port}/")

        self.ts.Disk.records_config.update({
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
            "proxy.config.http.insert_forwarded": "for|by=ip|proto",
            "proxy.config.ssl.server.cert.path": f"{self.ts.Variables.SSLDir}",
            "proxy.config.ssl.server.private_key.path": f"{self.ts.Variables.SSLDir}",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol",
        })

        self.ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: access
      format: '%<chi> %<pps>'

  logs:
    - filename: access
      format: access
'''.split("\n"))

    def addTestCase0(self):
        """
        Incoming PROXY Protocol v1 on TCP port
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.httpbin)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Command = f"curl -vs --haproxy-protocol http://localhost:{self.ts.Variables.proxy_protocol_port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_0_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_0_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase1(self):
        """
        Incoming PROXY Protocol v1 on SSL port
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -vsk --haproxy-protocol --http1.1 https://localhost:{self.ts.Variables.proxy_protocol_ssl_port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_1_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_1_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase2(self):
        """
        Test with netcat
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"echo 'PROXY TCP4 198.51.100.1 198.51.100.2 51137 80\r\nGET /get HTTP/1.1\r\nHost: 127.0.0.1:80\r\n' | nc localhost {self.ts.Variables.proxy_protocol_port}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_2_stdout.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase3(self):
        """
        Verify ATS with :pp: server_ports designation can handle a connection
        without Proxy Protocol.
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -vs --http1.1 http://localhost:{self.ts.Variables.proxy_protocol_port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_3_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_3_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase4(self):
        """
        Verify ATS with :pp:ssl server_ports designation can handle a TLS
        connection without Proxy Protocol.
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -vsk --http1.1 https://localhost:{self.ts.Variables.proxy_protocol_ssl_port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_4_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_4_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase99(self):
        """
        check access log
        """
        Test.Disk.File(os.path.join(self.ts.Variables.LOGDIR, 'access.log'), exists=True, content='gold/access.gold')

        # Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self.ts.Variables.LOGDIR, 'access.log')
        )
        tr.Processes.Default.ReturnCode = 0

    def run(self):
        self.addTestCase0()
        self.addTestCase1()
        self.addTestCase2()
        self.addTestCase3()
        self.addTestCase4()
        self.addTestCase99()


class ProxyProtocolOutTest:
    """Test that ATS can send Proxy Protocol."""

    _pp_server = 'proxy_protocol_server.py'

    _dns_counter = 0
    _server_counter = 0
    _ts_counter = 0

    def __init__(self, pp_version: int, is_tunnel: bool) -> None:
        """Initialize a ProxyProtocolOutTest.

        :param pp_version: The Proxy Protocol version to use (1 or 2).
        :param is_tunnel: Whether ATS should tunnel to the origin.
        """

        if pp_version not in (-1, 1, 2):
            raise ValueError(
                f'Invalid Proxy Protocol version (not 1 or 2): {pp_version}')
        self._pp_version = pp_version
        self._is_tunnel = is_tunnel

    def setupOriginServer(self, tr: 'TestRun') -> None:
        """Configure the origin server.

        :param tr: The TestRun to associate the origin's Process with.
        """
        tr.Setup.CopyAs(self._pp_server, tr.RunDirectory)
        cert_file = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        key_file = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
        tr.Setup.Copy(cert_file)
        tr.Setup.Copy(key_file)
        server = tr.Processes.Process(
            f'server-{ProxyProtocolOutTest._server_counter}')
        ProxyProtocolOutTest._server_counter += 1
        server_port = get_port(server, "external_port")
        internal_port = get_port(server, "internal_port")
        command = (
            f'{sys.executable} {self._pp_server} '
            f'server.pem server.key 127.0.0.1 {server_port} {internal_port}')
        if not self._is_tunnel:
            command += ' --plaintext'
        server.Command = command
        server.Ready = When.PortOpenv4(server_port)

        self._server = server

    def setupDNS(self, tr: 'TestRun') -> None:
        """Configure the DNS server.

        :param tr: The TestRun to associate the DNS's Process with.
        """
        self._dns = tr.MakeDNServer(
            f'dns-{ProxyProtocolOutTest._dns_counter}',
            default='127.0.0.1')
        ProxyProtocolOutTest._dns_counter += 1

    def setupTS(self, tr: 'TestRun') -> None:
        """Configure Traffic Server."""
        process_name = f'ts-out-{ProxyProtocolOutTest._ts_counter}'
        ProxyProtocolOutTest._ts_counter += 1
        self._ts = tr.MakeATSProcess(process_name, enable_tls=True,
                                     enable_cache=False)

        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine(
            "dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key"
        )

        self._ts.Disk.remap_config.AddLine(
            f"map / http://backend.pp.origin.com:{self._server.Variables.external_port}/")

        self._ts.Disk.records_config.update({
            "proxy.config.ssl.server.cert.path": f"{self._ts.Variables.SSLDir}",
            "proxy.config.ssl.server.private_key.path": f"{self._ts.Variables.SSLDir}",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|proxyprotocol",
            "proxy.config.http.proxy_protocol_out": self._pp_version,
            "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.Variables.Port}",
            "proxy.config.dns.resolv_conf": 'NULL'
        })

        if self._is_tunnel:
            self._ts.Disk.records_config.update({
                "proxy.config.http.connect_ports": f'{self._server.Variables.external_port}',
            })

            self._ts.Disk.sni_yaml.AddLines([
                'sni:',
                '- fqdn: pp.origin.com',
                f'  tunnel_route: backend.pp.origin.com:{self._server.Variables.external_port}',
            ])

    def setLogExpectations(self, tr: 'TestRun') -> None:

        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            "HTTP/1.1 200 OK",
            "Verify that curl got a 200 response")

        if self._pp_version in (1, 2):
            expected_pp = (
                'PROXY TCP4 127.0.0.1 127.0.0.1 '
                rf'\d+ {self._ts.Variables.ssl_port}'
            )
            self._server.Streams.All += Testers.ContainsExpression(
                expected_pp,
                "Verify the server got the expected Proxy Protocol string.")

            self._server.Streams.All += Testers.ContainsExpression(
                f'Received Proxy Protocol v{self._pp_version}',
                "Verify the server got the expected Proxy Protocol version.")

        if self._pp_version == -1:
            self._server.Streams.All += Testers.ContainsExpression(
                'No Proxy Protocol string found',
                'There should be no Proxy Protocol string.')

    def run(self) -> None:
        """Run the test."""
        description = f'Proxy Protocol v{self._pp_version} '
        if self._is_tunnel:
            description += "with blind tunneling"
        else:
            description += "without blind tunneling"
        tr = Test.AddTestRun(description)

        self.setupDNS(tr)
        self.setupOriginServer(tr)
        self.setupTS(tr)

        self._ts.StartBefore(self._server)
        self._ts.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._ts)

        origin = f'pp.origin.com:{self._ts.Variables.ssl_port}'
        command = (
            'sleep1; curl -vsk --http1.1 '
            f'--resolve "{origin}:127.0.0.1" '
            f'https://{origin}/get'
        )

        tr.Processes.Default.Command = command
        tr.Processes.Default.ReturnCode = 0
        # Its only one transaction, so this should complete quickly. The test
        # server often hangs if there are issues parsing the Proxy Protocol
        # string.
        tr.TimeOut = 5
        self.setLogExpectations(tr)


ProxyProtocolTest().run()

ProxyProtocolOutTest(pp_version=-1, is_tunnel=False).run()
ProxyProtocolOutTest(pp_version=1, is_tunnel=False).run()
ProxyProtocolOutTest(pp_version=2, is_tunnel=False).run()
ProxyProtocolOutTest(pp_version=1, is_tunnel=True).run()
ProxyProtocolOutTest(pp_version=2, is_tunnel=True).run()
