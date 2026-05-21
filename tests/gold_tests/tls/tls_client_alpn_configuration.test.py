"""Verify ALPN to origin functionality."""

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

from typing import Optional

Test.Summary = __doc__


class TestAlpnFunctionality:
    """Define an object to test a set of ALPN functionality."""

    _replay_file: str = 'tls_client_alpn_configuration.replay.yaml'
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self,
            records_config_alpn: Optional[str] = None,
            conf_remap_alpn: Optional[str] = None,
            alpn_is_malformed: bool = False):
        """Declare the various test Processes.

        :param records_config_alpn: The string with which to configure the ATS
        ALPN via proxy.config.ssl.client.alpn_protocols in the records.yaml.
        If the paramenter is None, then no ALPN configuration will be
        explicitly set and ATS will use the default value.

        :param conf_remap_alpn: The string with which to configure the Traffic
        Server ALPN proxy.config.http.alpn_protocols configuration via
        conf_remap. If the parameter is None, then no conf_remap configuration
        will be set.

        :param alpn_is_malformed: If True, then the configured ALPN string in
        the records.yaml will be malformed. The TestRun will be configured to
        expect a warning and the server will be configured to receive no ALPN.
        """
        self._alpn = records_config_alpn
        self._alpn_conf_remap_alpn = conf_remap_alpn
        self._alpn_is_malformed = alpn_is_malformed

        configured_alpn = records_config_alpn if conf_remap_alpn is None else conf_remap_alpn
        if alpn_is_malformed:
            configured_alpn = None
        self._server = self._configure_server(configured_alpn)

        self._ts = self._configure_trafficserver(records_config_alpn, conf_remap_alpn, alpn_is_malformed)

    def _configure_server(self, expected_alpn: Optional[str] = None):
        """Configure the test server.

        :param expected_alpn: The ALPN expected from the client. If this is
        None, then the server will not expect an ALPN value.
        """
        server = Test.MakeVerifierServerProcess(f'server-{TestAlpnFunctionality._server_counter}', self._replay_file)
        TestAlpnFunctionality._server_counter += 1

        if expected_alpn is None:
            server.Streams.stdout = Testers.ContainsExpression('Negotiated ALPN: none', 'Verify that ATS sent no ALPN string.')
        else:
            protocols = expected_alpn.split(',')
            for protocol in protocols:
                server.Streams.stdout = Testers.ContainsExpression(
                    f'ALPN.*:.*{protocol}', 'Verify that the server parsed the configured ALPN string from ATS.')
        return server

    def _configure_trafficserver(
            self,
            records_config_alpn: Optional[str] = None,
            conf_remap_alpn: Optional[str] = None,
            alpn_is_malformed: bool = False):
        """Configure a Traffic Server process.

        :param records_config_alpn: See the description of this parameter in
        TestAlpnFunctionality._init__.
        """
        ts = Test.MakeATSProcess(f'ts-{TestAlpnFunctionality._ts_counter}', enable_tls=True, enable_cache=False)
        TestAlpnFunctionality._ts_counter += 1

        ts.addDefaultSSLFiles()
        ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": f'{ts.Variables.SSLDir}',
                "proxy.config.ssl.server.private_key.path": f'{ts.Variables.SSLDir}',
                "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',
                'proxy.config.diags.debug.enabled': 3,
                'proxy.config.diags.debug.tags': 'ssl|http',
            })

        if records_config_alpn is not None:
            ts.Disk.records_config.update({
                'proxy.config.ssl.client.alpn_protocols': records_config_alpn,
            })

        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        conf_remap_specification = ''
        if conf_remap_alpn is not None:
            conf_remap_specification = (
                '@plugin=conf_remap.so '
                f'@pparam=proxy.config.ssl.client.alpn_protocols={conf_remap_alpn}')

        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{self._server.Variables.https_port} {conf_remap_specification}')

        if alpn_is_malformed:
            ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR.*ALPN", "There should be no ALPN parse warnings.")
        else:
            ts.Disk.diags_log.Content += Testers.ExcludesExpression("ERROR.*ALPN", "There should be no ALPN parse warnings.")

        return ts

    def run(self):
        """Configure the TestRun."""
        description = "default" if self._alpn is None else self._alpn
        tr = Test.AddTestRun(f'ATS ALPN configuration: {description}')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{TestAlpnFunctionality._client_counter}', self._replay_file, https_ports=[self._ts.Variables.ssl_port])
        TestAlpnFunctionality._client_counter += 1


#
# Test default configuration.
#
TestAlpnFunctionality().run()

#
# Test various valid ALPN configurations.
#
TestAlpnFunctionality(records_config_alpn='http/1.1').run()
TestAlpnFunctionality(records_config_alpn='http/1.1,http/1.0').run()
TestAlpnFunctionality(records_config_alpn='http/1.1', conf_remap_alpn='http/1.1,http/1.0').run()
TestAlpnFunctionality(records_config_alpn='h2,http/1.1').run()
TestAlpnFunctionality(records_config_alpn='h2').run()

#
# Test malformed ALPN configurations.
#
TestAlpnFunctionality(records_config_alpn='not_a_protocol', alpn_is_malformed=True).run()
# Note that HTTP/3 to origin is not currently supported.
TestAlpnFunctionality(records_config_alpn='h3', alpn_is_malformed=True).run()
