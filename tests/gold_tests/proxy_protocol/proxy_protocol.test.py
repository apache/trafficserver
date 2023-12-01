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

Test.Summary = 'Test PROXY Protocol'
Test.ContinueOnFail = True


class ProxyProtocolInTest:
    """Test that ATS can receive Proxy Protocol."""

    replay_file = "replay/proxy_protocol_in.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("pp-in-server", self.replay_file)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts_in", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/")

        self.ts.Disk.records_config.update(
            {
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

    def runTraffic(self):
        tr = Test.AddTestRun("Verify correct handling of incoming PROXY header.")
        tr.AddVerifierClientProcess(
            "pp-in-client",
            self.replay_file,
            http_ports=[self.ts.Variables.proxy_protocol_port],
            https_ports=[self.ts.Variables.proxy_protocol_ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def checkAccessLog(self):
        """
        check access log
        """
        Test.Disk.File(os.path.join(self.ts.Variables.LOGDIR, 'access.log'), exists=True, content='gold/access.gold')

        # Wait for log file to appear, then wait one extra second to make sure
        # TS is done writing it.
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self.ts.Variables.LOGDIR, 'access.log'))
        tr.Processes.Default.ReturnCode = 0

    def run(self):
        self.runTraffic()
        self.checkAccessLog()


class ProxyProtocolOutTest:
    """Test that ATS can send Proxy Protocol."""

    _dns_counter = 0
    _client_counter = 0
    _server_counter = 0
    _ts_counter = 0
    _pp_out_replay_file = "replay/proxy_protocol_out.replay.yaml"

    def __init__(self, pp_version: int, is_tunnel: bool, is_tls_to_origin: bool = False) -> None:
        """Initialize a ProxyProtocolOutTest.

        :param pp_version: The Proxy Protocol version to use (1 or 2).
        :param is_tunnel: Whether ATS should tunnel to the origin.
        :param is_tls: Whether ATS should connect to the origin via TLS.
        """

        if pp_version not in (-1, 1, 2):
            raise ValueError(f'Invalid Proxy Protocol version (not 1 or 2): {pp_version}')
        self._pp_version = pp_version
        self._is_tunnel = is_tunnel
        self._is_tls_to_origin = is_tls_to_origin

    def setupOriginServer(self) -> None:
        """Configure the origin server.
        """
        self._server = Test.MakeVerifierServerProcess(
            f"pp-out-server-{ProxyProtocolOutTest._server_counter}", self._pp_out_replay_file)
        ProxyProtocolOutTest._server_counter += 1

    def setupDNS(self, tr: 'TestRun') -> None:
        """Configure the DNS server.

        :param tr: The TestRun to associate the DNS's Process with.
        """
        self._dns = tr.MakeDNServer(f'dns-{ProxyProtocolOutTest._dns_counter}', default='127.0.0.1')
        ProxyProtocolOutTest._dns_counter += 1

    def setupTS(self, tr: 'TestRun') -> None:
        """Configure Traffic Server."""
        process_name = f'ts-out-{ProxyProtocolOutTest._ts_counter}'
        ProxyProtocolOutTest._ts_counter += 1
        self._ts = tr.MakeATSProcess(process_name, enable_tls=True, enable_cache=False)

        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")
        scheme = 'https' if self._is_tls_to_origin else 'http'
        server_port = self._server.Variables.https_port if self._is_tls_to_origin else self._server.Variables.http_port
        self._ts.Disk.remap_config.AddLine(f"map / {scheme}://backend.pp.origin.com:{server_port}/")

        self._ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": f"{self._ts.Variables.SSLDir}",
                "proxy.config.ssl.server.private_key.path": f"{self._ts.Variables.SSLDir}",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|proxyprotocol",
                "proxy.config.http.proxy_protocol_out": self._pp_version,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.Variables.Port}",
                "proxy.config.dns.resolv_conf": 'NULL',
                "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE'
            })

        if self._is_tunnel:
            self._ts.Disk.records_config.update({
                "proxy.config.http.connect_ports": f'{self._server.Variables.https_port}',
            })

            self._ts.Disk.sni_yaml.AddLines(
                [
                    'sni:',
                    '- fqdn: pp.origin.com',
                    f'  tunnel_route: backend.pp.origin.com:{self._server.Variables.https_port}',
                ])

    def setLogExpectations(self, tr: 'TestRun') -> None:

        tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Verify the client got a 200 response.")

        if self._pp_version in (1, 2):
            expected_pp = ('PROXY TCP4 127.0.0.1 127.0.0.1 '
                           rf'\d+ {self._ts.Variables.ssl_port}')
            self._server.Streams.All += Testers.ContainsExpression(
                expected_pp, "Verify the server got the expected Proxy Protocol string.")

            self._server.Streams.All += Testers.ContainsExpression(
                f'Received PROXY header v{self._pp_version}', "Verify the server got the expected Proxy Protocol version.")

        if self._pp_version == -1:
            self._server.Streams.All += Testers.ContainsExpression(
                'No valid PROXY header found', 'There should be no Proxy Protocol string.')

        if self._is_tunnel:
            self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
                'CONNECT tunnel://backend.pp.origin.com', 'Verify ATS establishes a blind tunnel to the server.')

    def run(self) -> None:
        """Run the test."""
        description = f'Proxy Protocol v{self._pp_version} '
        if self._is_tunnel:
            description += "with blind tunneling"
        else:
            description += "without blind tunneling"
            if self._is_tls_to_origin:
                description += " on TLS connection to origin"
        tr = Test.AddTestRun(description)

        self.setupDNS(tr)
        self.setupOriginServer()
        self.setupTS(tr)

        tr.AddVerifierClientProcess(
            f"pp-out-client-{ProxyProtocolOutTest._client_counter}",
            self._pp_out_replay_file,
            http_ports=[self._ts.Variables.port],
            https_ports=[self._ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        ProxyProtocolOutTest._client_counter += 1
        self._ts.StartBefore(self._server)
        self._ts.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._server

        self.setLogExpectations(tr)


ProxyProtocolInTest().run()

# non-tunnling HTTP to origin
ProxyProtocolOutTest(pp_version=-1, is_tunnel=False, is_tls_to_origin=False).run()
ProxyProtocolOutTest(pp_version=1, is_tunnel=False, is_tls_to_origin=False).run()
ProxyProtocolOutTest(pp_version=2, is_tunnel=False, is_tls_to_origin=False).run()
# non-tunnling HTTPS to origin
ProxyProtocolOutTest(pp_version=-1, is_tunnel=False, is_tls_to_origin=True).run()
ProxyProtocolOutTest(pp_version=1, is_tunnel=False, is_tls_to_origin=True).run()
ProxyProtocolOutTest(pp_version=2, is_tunnel=False, is_tls_to_origin=True).run()
# tunneling
ProxyProtocolOutTest(pp_version=1, is_tunnel=True).run()
ProxyProtocolOutTest(pp_version=2, is_tunnel=True).run()
