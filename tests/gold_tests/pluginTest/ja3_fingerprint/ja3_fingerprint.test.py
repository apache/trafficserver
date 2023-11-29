'''
Verify the behavior of the JA3 fingerprint plugin.
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
import re

Test.Summary = __doc__


class JA3FingerprintTest:
    """Verify the behavior of the JA3 fingerprint plugin."""

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(self, test_remap: bool) -> None:
        """Configure the test processes in preparation for the TestRun.

        :param test_remap: Whether to configure the plugin as a remap plugin
        instead of as a global plugin.
        """
        self._test_remap = test_remap
        if test_remap:
            self._replay_file = 'ja3_fingerprint_remap.replay.yaml'
        else:
            self._replay_file = 'ja3_fingerprint_global.replay.yaml'

        tr = Test.AddTestRun('Testing ja3_fingerprint plugin.')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)
        self._await_ja3log()

    def _configure_dns(self, tr: 'TestRun') -> None:
        """Configure a nameserver for the test.

        :param tr: The TestRun to add the nameserver to.
        """
        name = f'dns{self._dns_counter}'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')
        JA3FingerprintTest._dns_counter += 1

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the server to be used in the test.

        :param tr: The TestRun to add the server to.
        """
        name = f'server{self._server_counter}'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        JA3FingerprintTest._server_counter += 1
        self._server.Streams.All += Testers.ContainsExpression(
            "https-request",
            "Verify the HTTPS request was received.")
        self._server.Streams.All += Testers.ContainsExpression(
            "http2-request",
            "Verify the HTTP/2 request was received.")

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        # Associate ATS with the Test so that metrics can be verified.
        name = f'ts{self._ts_counter}'
        self._ts = Test.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        JA3FingerprintTest._ts_counter += 1
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )
        server_port = self._server.Variables.https_port
        self._ts.Disk.remap_config.AddLine(
            f'map https://https.server.com https://https.backend.com:{server_port}'
        )

        if self._test_remap:
            self._ts.Disk.remap_config.AddLine(
                f'map https://http2.server.com https://http2.backend.com:{server_port} '
                '@plugin=ja3_fingerprint.so @pparam=--ja3log'
            )
        else:
            self._ts.Disk.plugin_config.AddLine('ja3_fingerprint.so --ja3log --ja3raw')
            self._ts.Disk.remap_config.AddLine(
                f'map https://http2.server.com https://http2.backend.com:{server_port}'
            )

        self._ts.Disk.records_config.update({
            'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
            'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',

            'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': 'NULL',

            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|ja3_fingerprint',
        })

        ja3log_path = os.path.join(self._ts.Variables.LOGDIR, "ja3_fingerprint.log")
        self._ts.Disk.File(ja3log_path, id='ja3_log')

        if self._test_remap:
            # Only the http2 request should be logged because only that request
            # had the plugin configured for that remap rule.
            regex = r'(.*JA3.*MD5){1}'
        else:
            regex = r'(.*JA3.*MD5){2}'

        self._ts.Disk.ja3_log.Content += Testers.ContainsExpression(
            regex,
            "Verify the JA3 log contains a JA3 line.",
            reflags=re.MULTILINE | re.DOTALL)

    def _configure_client(self, tr: 'TestRun') -> None:
        """Configure the TestRun.

        :param tr: The TestRun to add the client to.
        """
        name = f'client{self._client_counter}'
        p = tr.AddVerifierClientProcess(
            name,
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            https_ports=[self._ts.Variables.ssl_port])
        JA3FingerprintTest._client_counter += 1

        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        p.Streams.All += Testers.ContainsExpression(
            "https-response",
            "Verify the HTTPS response was received.")
        p.Streams.All += Testers.ContainsExpression(
            "http2-response",
            "Verify the HTTP/2 response was received.")

    def _await_ja3log(self) -> None:
        """Await the creation of the JA3 log."""
        tr = Test.AddTestRun('Await the contents of the JA3 log.')

        waiter = tr.Processes.Process('waiter', 'sleep 30')
        ja3_path = self._ts.Disk.ja3_log.AbsPath
        waiter.Ready = When.FileContains(ja3_path, "JA3")

        p = tr.Processes.Default
        p.Command = f'echo await {ja3_path} creation'
        p.StartBefore(waiter)


JA3FingerprintTest(test_remap=False)
JA3FingerprintTest(test_remap=True)
