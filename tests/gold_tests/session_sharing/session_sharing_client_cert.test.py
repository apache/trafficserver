'''
Test that a pooled outbound TLS session is only reused when the client
certificate the new transaction would present matches the one the session
authenticated with.
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

Test.Summary = '''
Outbound session reuse honors the client certificate.
'''

# The outbound client certificate is overridable per transaction, so two remap
# rules can reach one origin as two different identities. A pooled session that
# authenticated as one of them must not be handed to a transaction configured
# for the other, because the origin authorizes on the certificate presented
# during the handshake and there is no second handshake on reuse.
#
# These cases deliberately leave proxy.config.http.server_session_sharing.match
# unset so they exercise the shipped default rather than a value chosen here.
#
# Both certificates embed their private key and share one signer, so the origin
# accepts either and the only variable is whether the pool reuses the session.
#
# Only the outbound connection is TLS. Traffic into the proxy is plain HTTP, so
# no inbound server certificate is configured.

POOL_HIT = "global pool search successful"
CERT_ONE = "signed-foo.pem"
CERT_TWO = "signed-san.pem"


class ClientCertSharingTest:
    counter = 0

    def __init__(self, summary, require_client_cert):
        ClientCertSharingTest.counter += 1
        self._n = ClientCertSharingTest.counter
        self._tr = Test.AddTestRun(summary)
        self._require_client_cert = require_client_cert
        self._setup_origin()
        self._setup_ts()

    def _setup_origin(self):
        if self._require_client_cert:
            self._server = Test.MakeOriginServer(
                f"server{self._n}",
                ssl=True,
                options={
                    "--clientCA": f"{Test.RunDirectory}/signer.pem",
                    "--clientverify": ""
                },
                clientcert=f"{Test.RunDirectory}/{CERT_ONE}",
                clientkey=f"{Test.RunDirectory}/signed-foo.key")
        else:
            self._server = Test.MakeOriginServer(f"server{self._n}", ssl=True)

        # The origin readiness probe opens its own TLS connection, so the signer
        # and the certificate it presents have to exist in the run directory.
        self._server.Setup.Copy("../tls/ssl/signer.pem")
        self._server.Setup.Copy(f"../tls/ssl/{CERT_ONE}")
        self._server.Setup.Copy("../tls/ssl/signed-foo.key")

        request_header = {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        response_header = {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        self._server.addResponse("sessionlog.json", request_header, response_header)

    def _setup_ts(self):
        self._ts = Test.MakeATSProcess(f"ts{self._n}")
        for name in ("signer.pem", CERT_ONE, CERT_TWO):
            self._ts.Setup.CopyAs(f"../tls/ssl/{name}", self._ts.Variables.SSLDir)

        port = self._server.Variables.SSL_Port
        if self._require_client_cert:
            # Two identities, one origin. Same address, same host, different
            # certificate. Only the certificate distinguishes the two rules.
            self._ts.Disk.remap_config.AddLine(
                f'map /one https://127.0.0.1:{port}/ @plugin=conf_remap.so '
                f'@pparam=proxy.config.ssl.client.cert.filename={CERT_ONE}')
            self._ts.Disk.remap_config.AddLine(
                f'map /two https://127.0.0.1:{port}/ @plugin=conf_remap.so '
                f'@pparam=proxy.config.ssl.client.cert.filename={CERT_TWO}')
        else:
            self._ts.Disk.remap_config.AddLine(f'map /one https://127.0.0.1:{port}/')
            self._ts.Disk.remap_config.AddLine(f'map /two https://127.0.0.1:{port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http_ss',
                'proxy.config.http.server_session_sharing.pool': 'global',
                'proxy.config.ssl.client.cert.path': self._ts.Variables.SSLDir,
                # The origin certificate is self-signed. Verification is off
                # rather than PERMISSIVE so no expected warning is logged, which
                # leaves the default diags scan at its full strength instead of
                # weakening it to errors only.
                'proxy.config.ssl.client.verify.server.policy': 'DISABLED',
            })

    def _run(self, first_path, second_path):
        port = self._ts.Variables.port
        self._tr.MakeCurlCommandMulti(
            f'{{curl}} -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{port}{first_path} &&'
            f'{{curl}} -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{port}{second_path}',
            ts=self._ts)
        self._tr.Processes.Default.ReturnCode = 0
        # curl is not invoked with -f, so a non-200 would still exit 0. Pin the
        # responses so a case cannot pass on an error page from the proxy.
        self._tr.Processes.Default.Streams.stderr = "gold/200_two.gold"
        self._tr.Processes.Default.StartBefore(self._server)
        self._tr.Processes.Default.StartBefore(self._ts)

    def expect_reuse(self, first_path, second_path, reason):
        self._run(first_path, second_path)
        self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(POOL_HIT, reason)

    def expect_no_reuse(self, first_path, second_path, reason, proposed_cert, session_cert):
        self._run(first_path, second_path)
        self._ts.Disk.traffic_out.Content = Testers.ExcludesExpression(POOL_HIT, reason)
        # The absence of a pool hit is also what a first request that never
        # pooled anything looks like, so require the certificate comparison that
        # rejected the candidate. This line is logged only when a candidate was
        # found and tested.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f"validate_cert proposed_cert={proposed_cert}, cert={session_cert}",
            "Verify the pooled session was rejected on the client certificate")


# The security property: two identities to one origin must not share a session.
test = ClientCertSharingTest("Different client certificates do not share an outbound session", require_client_cert=True)
test.expect_no_reuse(
    '/one', '/two', "Verify a session is not reused across client certificates", proposed_cert=CERT_TWO, session_cert=CERT_ONE)

# Control: the same identity must still pool, so the fix does not simply
# disable outbound session reuse.
test = ClientCertSharingTest("The same client certificate shares an outbound session", require_client_cert=True)
test.expect_reuse('/one', '/one', "Verify a session is reused for the same client certificate")

# Regression guard for deployments that present no client certificate at all.
# Reuse here must keep working, so a later change to how an absent certificate
# is compared cannot quietly disable pooling for ordinary TLS origins.
test = ClientCertSharingTest("A TLS origin with no client certificate still shares a session", require_client_cert=False)
test.expect_reuse('/one', '/two', "Verify reuse is unaffected when no client certificate is configured")
