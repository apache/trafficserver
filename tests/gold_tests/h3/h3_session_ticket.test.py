'''
Verify HTTP/3 QUIC TLS session ticket handling.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may
#  obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os
import shlex

Test.Summary = '''
Verify that HTTP/3 QUIC connections can receive and offer TLS session tickets.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QUIC'),
    Condition.HasOpenSSLVersion('3.5.0'),
    Condition.HasOpenSSLQuicClient(),
)
Test.Setup.Copy('../tls/file.ticket')


def add_default_ssl_multicert(ts):
    """Configure the default server certificate."""
    if hasattr(ts.Disk, "ssl_multicert_yaml"):
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
    else:
        ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")


class TestHttp3SessionTicket:
    """Configure an HTTP/3 QUIC TLS session ticket test."""

    def __init__(self, name: str):
        """Initialize the test."""
        self.name = name
        self.session_file = os.path.join(Test.RunDirectory, "h3-quic-session.pem")
        self.ticket_file = os.path.join(Test.RunDirectory, "file.ticket")
        self._configure_traffic_server()
        self._configure_ticket_save()
        self._configure_ticket_reuse()

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        ts = Test.MakeATSProcess("ts", enable_tls=True, enable_quic=True, enable_cache=False)
        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        add_default_ssl_multicert(ts)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'quic|ssl',
                'proxy.config.quic.server.stateless_retry_enabled': 0,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.session_ticket.number': 2,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
            })

        self._ts = ts

    def _s_client_command(self, session_option: str):
        """Build an OpenSSL QUIC client command for ticket save or reuse."""
        script = os.path.join(Test.TestDirectory, "h3_session_ticket.sh")
        return f"{shlex.quote(script)} {session_option} {shlex.quote(self.session_file)} {self._ts.Variables.ssl_port}"

    def _check_s_client_handshake(self, tr):
        """Verify that OpenSSL completed the QUIC handshake."""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            "CONNECTION ESTABLISHED", "OpenSSL should complete the QUIC handshake.")
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            "Protocol version: QUICv1", "OpenSSL should negotiate QUICv1.")

    def _configure_ticket_save(self):
        """Configure the ticket save test run."""
        tr = Test.AddTestRun(self.name)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = f"rm -f {shlex.quote(self.session_file)}; {self._s_client_command('-sess_out')}"
        self._check_s_client_handshake(tr)
        tr.StillRunningAfter = self._ts

    def _configure_ticket_reuse(self):
        """Configure the ticket reuse test run."""
        tr = Test.AddTestRun("OpenSSL QUIC offers saved session ticket")
        tr.Processes.Default.Command = self._s_client_command("-sess_in")
        self._check_s_client_handshake(tr)
        tr.StillRunningAfter = self._ts


TestHttp3SessionTicket("OpenSSL QUIC saves session ticket")
