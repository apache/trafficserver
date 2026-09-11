'''
Test RFC 7250 raw public key (RPK) TLS between two ATS instances.
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
Test raw public keys (RFC 7250) on ATS-to-ATS (layered cache) TLS hops.
'''

# RPK is only compiled in when the linked TLS library supports it, so skip rather
# than fail where it is unavailable.
Test.SkipUnless(Condition.HasATSFeature('TS_USE_RPK'))

server = Test.MakeOriginServer("server")
request_header = {'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
response_header = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993',
    'body': 'origin response'
}
server.addResponse("sessionlog.json", request_header, response_header)


def make_parent(name, rpk_enabled, client_rpk_ca_file=None, client_cert_level=0, client_ca_file=None, sni_client_ca_file=None):
    """An upstream (parent) ATS: terminates TLS from the edge, forwards to the origin.

    `client_rpk_ca_file`, if set, configures ssl_client_rpk_ca_name to pin the edge's raw public
    key for mTLS; `client_cert_level` then requires/requests a client cert accordingly.
    `client_ca_file`, if set, is the CA bundle X.509 client certificates are verified against.
    `sni_client_ca_file`, if set, adds a sni.yaml verify_client action carrying that CA bundle as a
    per-connection override, which is what installs a per-connection verify store.
    """
    ts = Test.MakeATSProcess(name, enable_tls=True)
    ts.addSSLfile("ssl/server.pem")
    ts.addSSLfile("ssl/server.key")
    if client_rpk_ca_file is not None:
        ts.addSSLfile("ssl/{0}".format(client_rpk_ca_file))
    if client_ca_file is not None:
        ts.addSSLfile("ssl/{0}".format(client_ca_file))
    ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))
    multicert_lines = [
        'ssl_multicert:',
        '  - dest_ip: "*"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
    ]
    if rpk_enabled:
        multicert_lines.append('    ssl_rpk_enabled: 1')
    if client_rpk_ca_file is not None:
        # The file name is deliberately bare here (not ts.Variables.SSLDir-prefixed) to exercise
        # that ssl_client_rpk_ca_name resolves against proxy.config.ssl.CA.cert.path, matching
        # the equivalent resolution ssl_ca_name already gets.
        multicert_lines.append('    ssl_client_rpk_ca_name: {0}'.format(client_rpk_ca_file))
    ts.Disk.ssl_multicert_yaml.AddLines(multicert_lines)
    if sni_client_ca_file is not None:
        ts.addSSLfile("ssl/{0}".format(sni_client_ca_file))
        # Keyed on the name in server.pem, which is the SNI the client below sends. An IP literal is
        # never sent as SNI, so an entry keyed on one could not match.
        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                '- fqdn: random.server.com',
                '  verify_client: STRICT',
                '  verify_client_ca_certs: {0}/{1}'.format(ts.Variables.SSLDir, sni_client_ca_file),
            ])
    records = {
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_verify|ssl_load',
    }
    if client_rpk_ca_file is not None:
        # ssl_client_rpk_ca_name resolves against this, which is what the bare file name above
        # exercises.
        records['proxy.config.ssl.CA.cert.path'] = '{0}'.format(ts.Variables.SSLDir)
    if client_cert_level:
        records['proxy.config.ssl.client.certification_level'] = client_cert_level
        records['proxy.config.ssl.CA.cert.path'] = '{0}'.format(ts.Variables.SSLDir)
    if client_ca_file is not None:
        records['proxy.config.ssl.CA.cert.filename'] = client_ca_file
    ts.Disk.records_config.update(records)
    return ts


def make_edge(
        name,
        parent,
        pin_file,
        policy='ENFORCED',
        offer_client_rpk=False,
        offer_client_x509=False,
        client_cert="server.pem",
        client_key="server.key",
        server_ca_file=None):
    """A downstream (edge) ATS: connects to `parent` over TLS, pinning its raw public key.

    `offer_client_rpk`, if set, also offers a raw public key (derived from ssl/server.pem/.key,
    the same identity the edge uses inbound) as its own client cert toward `parent`, for `parent`
    to pin via ssl_client_rpk_ca_name. `offer_client_x509`, if set instead, offers a classic X.509
    client cert -- `client_cert` names which one, defaulting to the edge's own identity.
    """
    ts = Test.MakeATSProcess(name, enable_tls=True)
    ts.addSSLfile("ssl/server.pem")
    ts.addSSLfile("ssl/server.key")
    ts.addSSLfile("ssl/server.pubkey.pem")
    ts.addSSLfile("ssl/server.wrongpubkey.pem")
    if client_cert != "server.pem":
        ts.addSSLfile("ssl/{0}".format(client_cert))
        ts.addSSLfile("ssl/{0}".format(client_key))
    if server_ca_file is not None:
        ts.addSSLfile("ssl/{0}".format(server_ca_file))
    ts.Disk.remap_config.AddLine('map / https://127.0.0.1:{0}'.format(parent.Variables.ssl_port))
    ts.Disk.ssl_multicert_yaml.AddLines(
        [
            'ssl_multicert:',
            '  - dest_ip: "*"',
            '    ssl_cert_name: server.pem',
            '    ssl_key_name: server.key',
        ])
    ts.Disk.records_config.update(
        {
            'proxy.config.http.cache.http': 0,
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.client.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl_verify',
            'proxy.config.ssl.client.verify.server.policy': policy,
            # Pin the exact key instead of matching a name: a raw public key carries no SAN.
            'proxy.config.ssl.client.verify.server.properties': 'SIGNATURE',
        })
    if server_ca_file is not None:
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
                'proxy.config.ssl.client.CA.cert.filename': server_ca_file,
            })
    if pin_file is not None or offer_client_rpk or offer_client_x509:
        sni_lines = [
            'sni:',
            '- fqdn: 127.0.0.1',
        ]
        if pin_file is not None:
            sni_lines.append('  server_rpk_ca: {0}/{1}'.format(ts.Variables.SSLDir, pin_file))
        if offer_client_rpk:
            sni_lines += [
                '  client_cert: server.pem',
                '  client_key: server.key',
                '  client_rpk_enabled: true',
            ]
        elif offer_client_x509:
            sni_lines += [
                '  client_cert: {0}'.format(client_cert),
                '  client_key: {0}'.format(client_key),
            ]
        ts.Disk.sni_yaml.AddLines(sni_lines)
    return ts


# 1. Both hops speak RPK and the pin matches -> RPK is negotiated and accepted.
parent_rpk = make_parent("parent_rpk", rpk_enabled=True)
edge_ok = make_edge("edge_ok", parent_rpk, "server.pubkey.pem")

# 2. The parent has not been upgraded (no RPK), the edge is configured for it ->
#    negotiation must fall back to X.509 rather than failing. This is the steady state
#    for the whole duration of a rolling upgrade. This covers negotiation only: the edge does not
#    trust server.pem, so the chain check fails and PERMISSIVE is what lets the request through.
#    Scenarios 11 and 12 cover the chain verdict itself, under ENFORCED.
parent_x509 = make_parent("parent_x509", rpk_enabled=False)
edge_fallback = make_edge("edge_fallback", parent_x509, "server.pubkey.pem", policy='PERMISSIVE')

# 3. The pin does not match the key the parent presents -> rejected under ENFORCED.
edge_badpin = make_edge("edge_badpin", parent_rpk, "server.wrongpubkey.pem")

# 4. Same mismatch under PERMISSIVE -> warned about, but the request still succeeds.
edge_badpin_permissive = make_edge("edge_badpin_permissive", parent_rpk, "server.wrongpubkey.pem", policy='PERMISSIVE')

# 5. mTLS: the parent requires and pins the edge's raw public key, and the pin matches.
parent_mtls = make_parent("parent_mtls", rpk_enabled=True, client_rpk_ca_file="server.pubkey.pem", client_cert_level=2)
edge_mtls = make_edge("edge_mtls", parent_mtls, "server.pubkey.pem", offer_client_rpk=True)

# 6. mTLS: same setup, but the parent pins a different key than the edge actually offers ->
#    a required client cert is always fatal, unlike verify_server_policy which has a
#    PERMISSIVE mode -- there is no equivalent "warn only" mode for inbound mTLS.
parent_mtls_badpin = make_parent(
    "parent_mtls_badpin", rpk_enabled=True, client_rpk_ca_file="server.wrongpubkey.pem", client_cert_level=2)
edge_mtls_badpin = make_edge("edge_mtls_badpin", parent_mtls_badpin, "server.pubkey.pem", offer_client_rpk=True)

# 7. Per-entry scoping: the parent has two multicert entries -- the default ("dest_ip: *") entry
#    stays plain X.509 mTLS, while a more specific ("dest_ip: 127.0.0.1") entry pins the edge's raw
#    public key. Every connection here lands on the specific entry (IP match beats wildcard), so
#    this only succeeds if that entry's own client_rpk_ca config reaches the connection -- not the
#    default entry's classic X.509-only verify path SSL_new() started the connection from.
parent_scoped = Test.MakeATSProcess("parent_scoped", enable_tls=True)
parent_scoped.addSSLfile("ssl/server.pem")
parent_scoped.addSSLfile("ssl/server.key")
parent_scoped.addSSLfile("ssl/server.pubkey.pem")
parent_scoped.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))
parent_scoped.Disk.ssl_multicert_yaml.AddLines(
    [
        'ssl_multicert:',
        '  - dest_ip: "*"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
        '  - dest_ip: "127.0.0.1"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
        '    ssl_rpk_enabled: 1',
        '    ssl_client_rpk_ca_name: server.pubkey.pem',
    ])
parent_scoped.Disk.records_config.update(
    {
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(parent_scoped.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(parent_scoped.Variables.SSLDir),
        'proxy.config.ssl.client.certification_level': 2,
        'proxy.config.ssl.CA.cert.path': '{0}'.format(parent_scoped.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_verify|ssl_load',
    })
edge_scoped = make_edge("edge_scoped", parent_scoped, "server.pubkey.pem", offer_client_rpk=True)

# 8. X.509 fallback on an RPK-enabled entry: the client presents a classic X.509 client cert
#    instead of a raw public key. ssl_custom_verify_client_callback()'s X.509 fallback branch has
#    to accept it via BoringSSL's already-parsed chain (SSL_get_peer_full_cert_chain()), since
#    installing custom_verify (mandatory for the RPK branch above to work at all) disables
#    BoringSSL's own automatic X.509 verification for this ctx entirely.
parent_mtls_x509 = make_parent(
    "parent_mtls_x509",
    rpk_enabled=True,
    client_rpk_ca_file="server.pubkey.pem",
    client_cert_level=2,
    # server.pem is self-signed, so trusting it directly lets the edge's client cert (the same
    # file) verify successfully.
    client_ca_file="server.pem")
edge_mtls_x509 = make_edge("edge_mtls_x509", parent_mtls_x509, "server.pubkey.pem", offer_client_x509=True)

# 9. The X.509 fallback must still reject a chain it cannot build. The edge offers a client cert
#    signed by signer.pem, which this parent does not trust.
parent_mtls_untrusted = make_parent(
    "parent_mtls_untrusted",
    rpk_enabled=True,
    client_rpk_ca_file="server.pubkey.pem",
    client_cert_level=2,
    client_ca_file="server.pem")
edge_mtls_untrusted = make_edge(
    "edge_mtls_untrusted",
    parent_mtls_untrusted,
    "server.pubkey.pem",
    offer_client_x509=True,
    client_cert="signed-foo.pem",
    client_key="signed-foo.key")

# 10. The X.509 fallback must enforce certificate purpose, not just chain signatures. server.ocsp.pem
#     chains cleanly to ca.ocsp.pem but carries extendedKeyUsage = serverAuth only, so it must not
#     authenticate as a client. Without X509_STORE_CTX_set_default(..., "ssl_client") the purpose
#     check never runs and this connection is accepted.
parent_mtls_purpose = make_parent(
    "parent_mtls_purpose",
    rpk_enabled=True,
    client_rpk_ca_file="server.pubkey.pem",
    client_cert_level=2,
    client_ca_file="ca.ocsp.pem")
edge_mtls_purpose = make_edge(
    "edge_mtls_purpose",
    parent_mtls_purpose,
    "server.pubkey.pem",
    offer_client_x509=True,
    client_cert="server.ocsp.pem",
    client_key="server.ocsp.key")

# 11. Outbound X.509 fallback under ENFORCED, chain does not verify: the parent offers no raw public
#     key, so the edge's custom_verify path takes its X.509 fallback, and the edge does not trust
#     the parent's self-signed certificate. ENFORCED must reject rather than serve.
edge_fallback_untrusted = make_edge("edge_fallback_untrusted", parent_x509, "server.pubkey.pem", policy='ENFORCED')

# 12. The same path with a chain that does verify -- the rolling upgrade case the feature exists to
#     serve, which until now was only exercised under PERMISSIVE where the verdict is discarded.
edge_fallback_enforced = make_edge(
    "edge_fallback_enforced", parent_x509, "server.pubkey.pem", policy='ENFORCED', server_ca_file="server.pem")

# 13. A verify_client action overrides the client CA on an entry that also pins raw public keys, so
#     the X.509 fallback has to verify against the store that action installed rather than the one on
#     the SSL_CTX. The global CA (server.pem) does not sign this client's certificate and the
#     per-SNI override (signer.pem) does, so the connection only succeeds if the per-connection store
#     is what the fallback used. Driven by curl rather than an edge, because an ATS hop toward an IP
#     literal sends no SNI and so could never match the action. This covers the per-connection store
#     plumbing; the failure paths that can leave it stale need the rebuild itself to fail and are not
#     reachable from a test.
parent_sni_ca = make_parent(
    "parent_sni_ca",
    rpk_enabled=True,
    client_rpk_ca_file="server.pubkey.pem",
    client_cert_level=2,
    client_ca_file="server.pem",
    sni_client_ca_file="signer.pem")
parent_sni_ca.addSSLfile("ssl/signed-foo.pem")
parent_sni_ca.addSSLfile("ssl/signed-foo.key")

tr = Test.AddTestRun("RPK negotiated and pin matches")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_ok.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(parent_rpk)
tr.Processes.Default.StartBefore(edge_ok)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('origin response', 'the request should succeed end to end')
edge_ok.Disk.traffic_out.Content = Testers.ContainsExpression(
    'Origin authenticated with a raw public key .*pin match=yes', 'the hop should use RPK, not fall back to X.509')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_rpk
tr.StillRunningAfter += edge_ok

tr = Test.AddTestRun("falls back to X.509 against a parent without RPK support")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_fallback.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_x509)
tr.Processes.Default.StartBefore(edge_fallback)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('origin response', 'the request should still succeed')
# No RPK was negotiated, so the RPK branch must never run for this hop.
edge_fallback.Disk.traffic_out.Content = Testers.ExcludesExpression(
    'Origin authenticated with a raw public key', 'the hop should quietly negotiate X.509 instead')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_x509
tr.StillRunningAfter += edge_fallback

tr = Test.AddTestRun("pin mismatch is fatal under ENFORCED")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_badpin.Variables.ssl_port))
# curl sees a 5xx from the edge (upstream connect failed) rather than a transport error.
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(edge_badpin)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression('origin response', 'the request must not be served')
edge_badpin.Disk.traffic_out.Content = Testers.ContainsExpression(
    'Origin authenticated with a raw public key .*pin match=no', 'the offered key should not match the pin')
# Warning() goes to diags.log, not traffic.out (which only carries Dbg() debug output).
edge_badpin.Disk.diags_log.Content = Testers.ContainsExpression(
    'Origin raw public key did not match any trusted key. Action=Terminate',
    'an unmatched pin must terminate the connection under ENFORCED')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_rpk

tr = Test.AddTestRun("pin mismatch only warns under PERMISSIVE")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_badpin_permissive.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(edge_badpin_permissive)
tr.Processes.Default.Streams.All = Testers.ContainsExpression('origin response', 'the request should still be served')
edge_badpin_permissive.Disk.diags_log.Content = Testers.ContainsExpression(
    'Origin raw public key did not match any trusted key. Action=Continue', 'an unmatched pin must only warn under PERMISSIVE')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_rpk

tr = Test.AddTestRun("mTLS: parent pins the edge's raw public key and it matches")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_mtls.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_mtls)
tr.Processes.Default.StartBefore(edge_mtls)
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'origin response', 'the request should succeed once the client cert pin matches')
parent_mtls.Disk.diags_log.Content = Testers.ExcludesExpression(
    'client raw public key did not match any trusted key', 'a matching pin must not warn')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_mtls
tr.StillRunningAfter += edge_mtls

tr = Test.AddTestRun("mTLS: a required client cert pin mismatch is always fatal")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_mtls_badpin.Variables.ssl_port))
# curl sees a 5xx from the edge (upstream mTLS handshake failed) rather than a transport error.
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_mtls_badpin)
tr.Processes.Default.StartBefore(edge_mtls_badpin)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression('origin response', 'the request must not be served')
parent_mtls_badpin.Disk.diags_log.Content = Testers.ContainsExpression(
    'client raw public key did not match any trusted key', 'the offered client key should not match the pin')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_mtls_badpin
tr.StillRunningAfter += edge_mtls_badpin

tr = Test.AddTestRun("per-entry scoping: a specific entry's client_rpk_ca reaches the connection")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_scoped.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_scoped)
tr.Processes.Default.StartBefore(edge_scoped)
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'origin response', 'the request should succeed on the specific entry, not the default entry')
# Confirms the RPK-aware verify path from the *matched* entry actually ran on this connection --
# the default entry has no client_rpk_ca, so if settings leaked from it instead, this would never
# appear and the classic X.509-only path would reject the edge's raw public key outright.
# BoringSSL takes the custom_verify path; OpenSSL's classic callback logs the RPK branch directly.
parent_scoped.Disk.traffic_out.Content = Testers.ContainsExpression(
    'Callback: custom verify client cert|Client authenticated with a raw public key',
    'the matched entry, not the default entry, must drive verification')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_scoped
tr.StillRunningAfter += edge_scoped

tr = Test.AddTestRun("X.509 fallback: a classic client cert verifies on an RPK-enabled entry")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_mtls_x509.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_mtls_x509)
tr.Processes.Default.StartBefore(edge_mtls_x509)
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'origin response', 'a valid X.509 client cert should still verify on an RPK-enabled entry')
# Confirms client-cert verification actually engaged for this connection, rather than some other,
# unrelated path silently accepting the chain. BoringSSL takes the custom_verify path (mandatory
# for the RPK branch above) and its hand-rolled X.509 fallback; OpenSSL has no such split -- the
# same classic callback handles both RPK and X.509 natively, so its entry log is the signal there.
parent_mtls_x509.Disk.traffic_out.Content = Testers.ContainsExpression(
    'Callback: custom verify client cert|Callback: verify client cert', 'a client-cert verify callback must run for this entry')
parent_mtls_x509.Disk.traffic_out.Content += Testers.ExcludesExpression(
    'client certificate chain verification failed', 'the X.509 fallback must accept a validly-signed chain')
parent_mtls_x509.Disk.traffic_out.Content += Testers.ExcludesExpression(
    'Client authenticated with a raw public key', 'this connection must take the X.509 path, not the RPK one')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_mtls_x509
tr.StillRunningAfter += edge_mtls_x509

tr = Test.AddTestRun("X.509 fallback: an untrusted client chain is rejected")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_mtls_untrusted.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_mtls_untrusted)
tr.Processes.Default.StartBefore(edge_mtls_untrusted)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
    'origin response', 'a client cert from an untrusted CA must not authenticate')
# Assert on the reason, not just the outcome: without this the scenario would also pass if the edge
# sent no client certificate at all, in which case the empty-chain early return fires and the
# hand-rolled verification under test never runs.
parent_mtls_untrusted.Disk.traffic_out.Content = Testers.ContainsExpression(
    'client certificate chain verification failed: unable to get local issuer certificate',
    'the chain must be rejected for having no trusted issuer')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_mtls_untrusted
tr.StillRunningAfter += edge_mtls_untrusted

tr = Test.AddTestRun("X.509 fallback: a serverAuth-only client cert is rejected")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_mtls_purpose.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_mtls_purpose)
tr.Processes.Default.StartBefore(edge_mtls_purpose)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
    'origin response', 'a serverAuth-only cert must not authenticate as a client')
# The purpose check is the whole point of this scenario: this chain verifies, so a generic failure
# here would mean something else rejected it and X509_STORE_CTX_set_default() went untested.
parent_mtls_purpose.Disk.traffic_out.Content = Testers.ContainsExpression(
    'client certificate chain verification failed: unsupported certificate purpose',
    'the chain must be rejected on purpose alone, not on its signatures')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_mtls_purpose
tr.StillRunningAfter += edge_mtls_purpose

tr = Test.AddTestRun("outbound X.509 fallback: an unverifiable origin chain is rejected under ENFORCED")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_fallback_untrusted.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(edge_fallback_untrusted)
tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
    'origin response', 'an origin chain that does not verify must not be served under ENFORCED')
edge_fallback_untrusted.Disk.diags_log.Content = Testers.ContainsExpression(
    'Core server certificate verification failed.*Action=Terminate', 'ENFORCED must terminate on the chain verdict itself')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_x509
tr.StillRunningAfter += edge_fallback_untrusted

tr = Test.AddTestRun("outbound X.509 fallback: a verifiable origin chain succeeds under ENFORCED")
tr.MakeCurlCommand('-k https://127.0.0.1:{0}/'.format(edge_fallback_enforced.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(edge_fallback_enforced)
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'origin response', 'a trusted origin chain must be served under ENFORCED during a rolling upgrade')
edge_fallback_enforced.Disk.traffic_out.Content = Testers.ExcludesExpression(
    'Origin authenticated with a raw public key', 'this hop must fall back to X.509, not negotiate RPK')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_x509
tr.StillRunningAfter += edge_fallback_enforced

tr = Test.AddTestRun("X.509 fallback honors a per-SNI client CA override on an RPK-enabled entry")
tr.MakeCurlCommand(
    "-k --resolve 'random.server.com:{0}:127.0.0.1' --cert {1}/signed-foo.pem --key {1}/signed-foo.key"
    " https://random.server.com:{0}/".format(parent_sni_ca.Variables.ssl_port, parent_sni_ca.Variables.SSLDir))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(parent_sni_ca)
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'origin response', 'a client cert issued by the per-SNI CA override must authenticate')
# The global CA cannot verify this chain, so reaching the origin proves the fallback used the store
# the verify_client action installed. A missing store fails closed instead.
parent_sni_ca.Disk.diags_log.Content = Testers.ExcludesExpression(
    'no per-connection client CA store', 'the per-connection store must be readable from the callback')
parent_sni_ca.Disk.traffic_out.Content = Testers.ExcludesExpression(
    'client certificate chain verification failed', 'the override must verify this chain')
tr.StillRunningAfter = server
tr.StillRunningAfter += parent_sni_ca
