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


def make_parent(name, rpk_enabled, client_rpk_ca_file=None, client_cert_level=0):
    """An upstream (parent) ATS: terminates TLS from the edge, forwards to the origin.

    `client_rpk_ca_file`, if set, configures ssl_client_rpk_ca_name to pin the edge's raw public
    key for mTLS; `client_cert_level` then requires/requests a client cert accordingly.
    """
    ts = Test.MakeATSProcess(name, enable_tls=True)
    ts.addSSLfile("ssl/server.pem")
    ts.addSSLfile("ssl/server.key")
    if client_rpk_ca_file is not None:
        ts.addSSLfile("ssl/{0}".format(client_rpk_ca_file))
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
    records = {
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_verify|ssl_load',
    }
    if client_cert_level:
        records['proxy.config.ssl.client.certification_level'] = client_cert_level
        records['proxy.config.ssl.CA.cert.path'] = '{0}'.format(ts.Variables.SSLDir)
    ts.Disk.records_config.update(records)
    return ts


def make_edge(name, parent, pin_file, policy='ENFORCED', offer_client_rpk=False):
    """A downstream (edge) ATS: connects to `parent` over TLS, pinning its raw public key.

    `offer_client_rpk`, if set, also offers a raw public key (derived from ssl/server.pem/.key,
    the same identity the edge uses inbound) as its own client cert toward `parent`, for `parent`
    to pin via ssl_client_rpk_ca_name.
    """
    ts = Test.MakeATSProcess(name, enable_tls=True)
    ts.addSSLfile("ssl/server.pem")
    ts.addSSLfile("ssl/server.key")
    ts.addSSLfile("ssl/server.pubkey.pem")
    ts.addSSLfile("ssl/server.wrongpubkey.pem")
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
    if pin_file is not None or offer_client_rpk:
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
        ts.Disk.sni_yaml.AddLines(sni_lines)
    return ts


# 1. Both hops speak RPK and the pin matches -> RPK is negotiated and accepted.
parent_rpk = make_parent("parent_rpk", rpk_enabled=True)
edge_ok = make_edge("edge_ok", parent_rpk, "server.pubkey.pem")

# 2. The parent has not been upgraded (no RPK), the edge is configured for it ->
#    negotiation must fall back to X.509 rather than failing. This is the steady state
#    for the whole duration of a rolling upgrade.
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
