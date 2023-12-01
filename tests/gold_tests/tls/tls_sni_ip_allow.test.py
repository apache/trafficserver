'''
Test exercising ip_allow configuration of sni.yaml
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

Test.Summary = '''Test sni.yaml ip_allow.'''


class ConnectionType:
    GET = 0
    TUNNEL = 1


class TestSniIpAllow:
    '''Verify ip_allow of sni.yaml.'''
    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(self, connect_type: int) -> None:
        """Configure a test run.
        :param connect_type: The type of connection to use.
        """
        tr = Test.AddTestRun("Verify ip_allow of sni.yaml")

        if connect_type == ConnectionType.GET:
            self._replay_file: str = "replay/ip_allow.replay.yaml"
        elif connect_type == ConnectionType.TUNNEL:
            self._replay_file: str = "replay/ip_allow_tunnel.replay.yaml"
        else:
            raise ValueError(f'Invalid connect_type: {connect_type}')

        self._dns = self._configure_dns(tr)
        self._server = self._configure_server(tr)
        self._ts = self._configure_trafficserver(tr, connect_type, self._dns, self._server)
        self._configure_client(tr, self._dns, self._server, self._ts)

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure a DNS for the TestRun.
        :param tr: The TestRun to configure with the DNS.
        :return: The DNS Process.
        """
        name = f'dns{TestSniIpAllow._dns_counter}'
        dns = tr.MakeDNServer(name, default='127.0.0.1')
        TestSniIpAllow._dns_counter += 1
        return dns

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure an Origin Server for the TestRun.
        :param tr: The TestRun to configure with the Origin Server.
        :return: The Origin Server Process.
        """
        name = f'server{TestSniIpAllow._server_counter}'
        server = tr.AddVerifierServerProcess(name, self._replay_file)
        TestSniIpAllow._server_counter += 1
        server.Streams.All += Testers.ContainsExpression('allowed-request', 'The allowed request should be recieved.')
        server.Streams.All += Testers.ExcludesExpression('blocked-request', 'The blocked request should not have been recieved.')
        server.Streams.All += Testers.ExcludesExpression(
            'block.me.com', 'Nothing about the block.me.com sni should have been recieved.')

        return server

    def _configure_trafficserver(self, tr: 'TestRun', connect_type: int, dns: 'Process', server: 'Process') -> 'Process':
        """Configure Traffic Server for the TestRun.
        :param tr: The TestRun to configure with Traffic Server.
        :param connect_type: The type of connection to use.
        :param dns: The DNS Process.
        :param server: The Origin Server Process.
        :return: The Traffic Server Process.
        """
        name = f'ts{TestSniIpAllow._ts_counter}'
        ts = tr.MakeATSProcess(name, enable_tls=True, enable_cache=False)
        TestSniIpAllow._ts_counter += 1
        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                '- fqdn: block.me.com',
                '  ip_allow: 192.168.10.1',  # Therefore 127.0.0.1 should be blocked.
            ])
        if connect_type == ConnectionType.TUNNEL:
            ts.Disk.sni_yaml.AddLines([
                f'  tunnel_route: backend.server.com:{server.Variables.https_port}',
            ])
        ts.Disk.sni_yaml.AddLines([
            '- fqdn: allow.me.com',
            '  ip_allow: 127.0.0.1',
        ])
        if connect_type == ConnectionType.TUNNEL:
            ts.Disk.sni_yaml.AddLines([
                f'  tunnel_route: backend.server.com:{server.Variables.https_port}',
            ])
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.remap_config.AddLine(f'map / http://remapped.backend.server.com:{server.Variables.http_port}/')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|ssl',
            })
        if connect_type == ConnectionType.TUNNEL:
            ts.Disk.records_config.update({
                'proxy.config.http.connect_ports': f"{server.Variables.https_port}",
            })
        return ts

    def _configure_client(self, tr: 'TestRun', dns: 'Process', server: 'Process', ts: 'Process') -> None:
        """Configure the client for the TestRun.
        :param tr: The TestRun to configure with the client.
        :param dns: The DNS Process.
        :param server: The Origin Server Process.
        :param ts: The Traffic Server Process.
        """
        name = f'client{TestSniIpAllow._client_counter}'
        p = tr.AddVerifierClientProcess(
            name, self._replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
        TestSniIpAllow._client_counter += 1
        ts.StartBefore(server)
        ts.StartBefore(dns)
        p.StartBefore(ts)

        # Because the first connection will be aborted, the client will have a
        # non-zero return code.
        p.ReturnCode = 1

        p.Streams.All += Testers.ContainsExpression('allowed-response', 'The response to teh allowed request should be recieved.')
        p.Streams.All += Testers.ExcludesExpression(
            'blocked-response', 'The response to the blocked request should not have been recieved.')


TestSniIpAllow(ConnectionType.GET)
TestSniIpAllow(ConnectionType.TUNNEL)
