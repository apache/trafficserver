'''
Verify the behavior of the jax_fingerprint plugin.

Covers all supported run modes (overwrite, keep, append), setup types
(global, remap, hybrid), fingerprint methods (JA3, JA4, JA4H), and both
HTTP/1.1 and HTTP/2 client connections.

Global setup:   plugin in plugin.config, applies to every request.
Remap setup:    plugin per remap rule, applies only to matched routes.
Hybrid setup:   global plugin captures the TLS client hello (creates
                context) while a remap plugin reads that context and
                sets headers only on matched routes.
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
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))


class JaxFingerprintTest:
    '''Verify the behavior of the jax_fingerprint plugin.'''

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self,
            name: str,
            method: str,
            setup: str,
            mode: str = 'overwrite',
            http2: bool = False,
            servernames: str = '',
            log_field: str = '') -> None:
        '''Configure test processes for the jax_fingerprint plugin.

        :param name: Descriptive name for this test run.
        :param method: Fingerprint method: 'JA3', 'JA4', or 'JA4H'.
        :param setup: Plugin setup type: 'global', 'remap', or 'hybrid'.
        :param mode: Header write mode: 'overwrite', 'keep', or 'append'.
        :param http2: If True, the client connects to ATS over HTTP/2 (h2
            over TLS).  For JA3/JA4 the h2 ALPN in the ClientHello produces
            a fingerprint distinct from the HTTP/1.1 case.  For JA4H,
            get_version() detects HTTP/2 via the protocol stack.
        :param servernames: Comma-separated SNI allowlist passed as
            --servernames to the plugin.  Connections whose SNI is not in
            the list are skipped entirely: handle_client_hello returns early,
            no context is created, and handle_read_request_hdr is a no-op.
            Only meaningful for CONNECTION_BASED methods (JA3/JA4) in global
            setup.
        :param log_field: Symbol name for --log-field option.  When set,
            configures logging.yaml with a custom format using the symbol
            and verifies the fingerprint appears in the ATS access log.
            Only supported with global setup.

        Method notes:
          - JA3 / JA4 are CONNECTION_BASED (triggered on TLS client hello)
            and require TLS between the client and ATS.
          - JA4H is REQUEST_BASED (triggered on HTTP request read) and
            works over plain HTTP, HTTPS, and HTTP/2.

        Setup notes:
          - global:  plugin.config entry with --standalone so ATS registers
            the READ_REQUEST_HDR hook and modifies every request.
          - remap:   @plugin in remap.config.  For CONNECTION_BASED methods
            --standalone must also be passed so the remap plugin registers
            the SSL_CLIENT_HELLO_HOOK globally and can populate the vconn
            context that TSRemapDoRemap reads later.
          - hybrid:  global plugin (no --standalone) captures the TLS client
            hello and stores context on the vconn; a remap plugin (no
            --standalone) reads that shared context and sets headers only
            on matched routes.  Both instances share the same user-arg slot
            because TSUserArgIndexReserve is idempotent for identical names.
        '''
        self._name = name
        self._method = method
        self._setup = setup
        self._mode = mode
        self._http2 = http2
        self._servernames = servernames
        self._log_field = log_field
        # HTTP/2 always runs over TLS (h2 requires TLS).
        self._needs_tls = method in ('JA3', 'JA4') or http2
        self._replay_file = self._choose_replay_file()

        tr = Test.AddTestRun(name)
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)
        Test.AddAwaitFileContainsTestRun(
            f'Await jax_fingerprint.log for: {self._name}', self._ts.Disk.jax_log.AbsPath, self._method)

        if self._log_field:
            log_field_path = os.path.join(self._ts.Variables.LOGDIR, 'jax_log_field.log')
            # Verify the log contains a fingerprint (not just a dash placeholder).
            Test.AddAwaitFileContainsTestRun(
                f'Await jax_log_field.log for: {self._name}', log_field_path, f'{self._method}: [a-z0-9]')

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _choose_replay_file(self) -> str:
        '''Return the replay YAML to drive this test.

        Key: (servernames, setup, needs_tls, http2, mode)
        '''
        key = (bool(self._servernames), self._setup, self._needs_tls, self._http2, self._mode)
        mapping = {
            (False, 'global', False, False, 'overwrite'): 'jax_fingerprint_global.replay.yaml',
            (False, 'global', False, False, 'keep'): 'jax_fingerprint_keep.replay.yaml',
            (False, 'global', False, False, 'append'): 'jax_fingerprint_append.replay.yaml',
            (False, 'global', True, False, 'overwrite'): 'jax_fingerprint_tls.replay.yaml',
            (False, 'global', True, True, 'overwrite'): 'jax_fingerprint_global_h2.replay.yaml',
            (False, 'remap', False, False, 'overwrite'): 'jax_fingerprint_remap.replay.yaml',
            (False, 'remap', True, False, 'overwrite'): 'jax_fingerprint_remap_tls.replay.yaml',
            (False, 'hybrid', True, False, 'overwrite'): 'jax_fingerprint_remap_tls.replay.yaml',
            (True, 'global', True, False, 'overwrite'): 'jax_fingerprint_servernames.replay.yaml',
            (True, 'hybrid', True, False, 'overwrite'): 'jax_fingerprint_hybrid_servernames.replay.yaml',
        }
        return mapping[key]

    def _build_remap_plugin_line(self, add_standalone: bool = False) -> str:
        '''Build the @plugin / @pparam fragment for a remap.config line.'''
        parts = [
            '@plugin=jax_fingerprint.so',
            '@pparam=--method',
            f'@pparam={self._method}',
            '@pparam=--header',
            '@pparam=x-jax',
            '@pparam=--via-header',
            '@pparam=x-jax-via',
            '@pparam=--log-filename',
            '@pparam=jax_fingerprint',
        ]
        if self._mode != 'overwrite':
            parts.extend(['@pparam=--mode', f'@pparam={self._mode}'])
        if add_standalone:
            parts.append('@pparam=--standalone')
        return ' '.join(parts)

    # ------------------------------------------------------------------
    # Test-process configuration
    # ------------------------------------------------------------------

    def _configure_dns(self, tr: 'TestRun') -> None:
        '''Configure a nameserver for the test.'''
        name = f'dns{JaxFingerprintTest._dns_counter}'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')
        JaxFingerprintTest._dns_counter += 1

    def _configure_server(self, tr: 'TestRun') -> None:
        '''Configure the origin (verifier) server.'''
        name = f'server{JaxFingerprintTest._server_counter}'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        JaxFingerprintTest._server_counter += 1

        # For remap / hybrid tests the second session reaches the server
        # with the fingerprint header already appended by ATS; verify it.
        if self._servernames:
            # Only the SNI-matched session carries the fingerprint header.
            uuid_key = 'hybrid-servernames-in-filter' if self._setup == 'hybrid' else 'servernames-in-filter'
            self._server.Streams.All += Testers.ContainsExpression(uuid_key, 'Verify the allowed-SNI request reached the server.')
            self._server.Streams.All += Testers.ContainsExpression(
                r'x-jax:', 'Verify the fingerprint header was forwarded for the allowed SNI.', reflags=re.IGNORECASE)
        elif self._setup in ('remap', 'hybrid'):
            uuid_key = 'remap-tls-plugin-request' if self._needs_tls else 'remap-plugin-request'
            self._server.Streams.All += Testers.ContainsExpression(uuid_key, 'Verify the matched-route request reached the server.')
            self._server.Streams.All += Testers.ContainsExpression(
                r'x-jax:', 'Verify the fingerprint header was forwarded.', reflags=re.IGNORECASE)
        else:
            # global tests - all requests carry the fingerprint header
            self._server.Streams.All += Testers.ContainsExpression(
                r'x-jax:', 'Verify the fingerprint header was forwarded.', reflags=re.IGNORECASE)

    def _configure_trafficserver(self) -> None:
        '''Configure Traffic Server and its plugin / remap rules.'''
        name = f'ts{JaxFingerprintTest._ts_counter}'
        self._ts = Test.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        JaxFingerprintTest._ts_counter += 1

        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        if self._needs_tls:
            server_port = self._server.Variables.https_port
            scheme = 'https'
        else:
            server_port = self._server.Variables.http_port
            scheme = 'http'

        self._ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.proxy_name': 'test.proxy.test',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'jax_fingerprint|http',
            })

        log_path = os.path.join(self._ts.Variables.LOGDIR, 'jax_fingerprint.log')
        self._ts.Disk.File(log_path, id='jax_log')
        self._ts.Disk.jax_log.Content += Testers.ContainsExpression(
            rf'.*{self._method}:.*', f'Verify the log contains a {self._method} fingerprint.', reflags=re.MULTILINE)

        backend = f'{scheme}://jax.backend.test:{server_port}'
        backend_no_plugin = f'{scheme}://jax.backend.test:{server_port}'

        if self._setup == 'global':
            global_args = (
                f'--method {self._method} '
                f'--header x-jax '
                f'--via-header x-jax-via '
                f'--log-filename jax_fingerprint '
                f'--standalone')
            if self._mode != 'overwrite':
                global_args += f' --mode {self._mode}'
            if self._servernames:
                global_args += f' --servernames {self._servernames}'
            if self._log_field:
                global_args += f' --log-field {self._log_field}'
            self._ts.Disk.plugin_config.AddLine(f'jax_fingerprint.so {global_args}')
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax.server.test {backend}')
            if self._servernames:
                # Second remap rule for the SNI that is NOT in the allowlist.
                self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax-filtered.server.test {backend}')

        elif self._setup == 'remap':
            # Route without plugin (session 1 in replay file)
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax-no-plugin.server.test {backend_no_plugin}')
            # Route with plugin (session 2 in replay file)
            # CONNECTION_BASED methods need --standalone so the remap plugin
            # registers the SSL_CLIENT_HELLO_HOOK to populate the vconn context.
            remap_line = self._build_remap_plugin_line(add_standalone=self._needs_tls)
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax.server.test {backend} {remap_line}')

        elif self._setup == 'hybrid':
            # Global plugin: registers SSL_CLIENT_HELLO_HOOK to capture the
            # TLS handshake and store context on the vconn.  No --standalone
            # means no READ_REQUEST_HDR hook, so headers are never set here.
            global_plugin_args = f'--method {self._method}'
            if self._servernames:
                global_plugin_args += f' --servernames {self._servernames}'
            self._ts.Disk.plugin_config.AddLine(f'jax_fingerprint.so {global_plugin_args}')
            remap_line = self._build_remap_plugin_line(add_standalone=False)
            if self._servernames:
                # Both routes have the remap plugin.  Only the SNI-allowed
                # connection has a context on the vconn, so only that request
                # gets fingerprint headers even though both routes are mapped.
                self._ts.Disk.remap_config.AddLine(
                    f'map https://jax.server.test https://jax.backend.test:{server_port} {remap_line}')
                self._ts.Disk.remap_config.AddLine(
                    f'map https://jax-filtered.server.test https://jax.backend.test:{server_port} {remap_line}')
            else:
                # Route without remap plugin: context is captured but no headers set.
                self._ts.Disk.remap_config.AddLine(f'map https://jax-no-plugin.server.test https://jax.backend.test:{server_port}')
                # Route with remap plugin: reads shared vconn context, sets headers.
                self._ts.Disk.remap_config.AddLine(
                    f'map https://jax.server.test https://jax.backend.test:{server_port} {remap_line}')

        if self._log_field:
            self._ts.Disk.logging_yaml.AddLines(
                f'''
logging:
  formats:
    - name: jax_custom
      format: '{self._method}: %<{self._log_field}>'
  logs:
    - filename: jax_log_field
      format: jax_custom
'''.split("\n"))

    def _configure_client(self, tr: 'TestRun') -> None:
        '''Configure the verifier client.'''
        name = f'client{JaxFingerprintTest._client_counter}'
        p = tr.AddVerifierClientProcess(
            name, self._replay_file, http_ports=[self._ts.Variables.port], https_ports=[self._ts.Variables.ssl_port])
        JaxFingerprintTest._client_counter += 1

        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts


# ======================================================================
# Test instances
# ======================================================================

# --- Global setup -------------------------------------------------------

# All HTTP/1.1 requests receive a JA4H fingerprint header (overwrite mode).
JaxFingerprintTest('Global JA4H overwrite', 'JA4H', 'global')

# All TLS requests receive a JA3 fingerprint header (overwrite mode).
JaxFingerprintTest('Global JA3 overwrite', 'JA3', 'global')

# --- Remap setup --------------------------------------------------------

# Only requests matching the remap rule receive a JA4H header.
JaxFingerprintTest('Remap JA4H', 'JA4H', 'remap')

# Remap plugin with --standalone captures TLS client hellos globally and
# sets JA3 headers only on the matched route.
JaxFingerprintTest('Remap JA3 standalone', 'JA3', 'remap')

# --- Hybrid setup -------------------------------------------------------

# Global plugin captures TLS client hellos (creates vconn context).
# Remap plugin reads that shared context and sets headers on matched routes.
JaxFingerprintTest('Hybrid JA4', 'JA4', 'hybrid')

# --- HTTP/2 (h2 over TLS) -----------------------------------------------

# JA4: the h2 ALPN in the ClientHello produces a different fingerprint than
# the HTTP/1.1 case – exercises the full TLS extension path with h2 ALPN.
JaxFingerprintTest('Global JA4 HTTP/2', 'JA4', 'global', http2=True)

# JA4H: exercises the HTTP/2 branch of get_version() which detects h2 via
# TSHttpTxnClientProtocolStackContains.
JaxFingerprintTest('Global JA4H HTTP/2', 'JA4H', 'global', http2=True)

# keep mode: existing x-jax / x-jax-via headers are not overwritten.
JaxFingerprintTest('Global JA4H keep mode', 'JA4H', 'global', mode='keep')

# append mode: fingerprint / proxy name are appended to existing header values.
JaxFingerprintTest('Global JA4H append mode', 'JA4H', 'global', mode='append')

# --- SNI allowlist (--servernames) --------------------------------------

# --servernames restricts fingerprinting to connections whose TLS SNI is in
# the list.  Connections with a non-matching SNI are skipped at the client-
# hello hook: no context is created and handle_read_request_hdr is a no-op,
# so neither the fingerprint header nor the via header are set.
JaxFingerprintTest('Global JA4 servernames', 'JA4', 'global', servernames='jax.server.test')

# --- Hybrid + SNI allowlist (most common production pattern) ---------------

# Global plugin captures TLS client hellos for allowed SNIs only.
# Remap plugin sets headers on both routes, but only the SNI-allowed
# connection has a vconn context, so only that request gets headers.
JaxFingerprintTest('Hybrid JA4 servernames', 'JA4', 'hybrid', servernames='jax.server.test')

# --- Custom log field (--log-field) -----------------------------------------

# Register a custom log field via --log-field and verify the fingerprint
# appears in the ATS access log configured in logging.yaml.
JaxFingerprintTest('Global JA4H log-field', 'JA4H', 'global', log_field='jaxja4h')
JaxFingerprintTest('Global JA4 log-field', 'JA4', 'global', log_field='jaxja4')

# ======================================================================
# All Methods Test - Verify shared context map works with multiple methods
# ======================================================================


class AllMethodsTest:
    '''Test multiple fingerprint methods loaded simultaneously.

    When multiple jax_fingerprint instances are loaded, they share user arg
    slots via a ContextMap. This test verifies that JA3/JA4 and JA4H can
    coexist and produce fingerprints while sharing that context machinery
    across their respective vconn and txn storage.
    '''

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(self, name: str) -> None:
        '''Configure test with multiple methods loaded.'''
        self._name = name
        self._replay_file = 'jax_fingerprint_all_methods.replay.yaml'

        tr = Test.AddTestRun(name)
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> None:
        '''Configure a nameserver for the test.'''
        name = f'dns_all{AllMethodsTest._dns_counter}'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')
        AllMethodsTest._dns_counter += 1

    def _configure_server(self, tr: 'TestRun') -> None:
        '''Configure the origin (verifier) server.'''
        name = f'server_all{AllMethodsTest._server_counter}'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        AllMethodsTest._server_counter += 1

        # Verify all headers were forwarded to the origin.
        for header in ['x-ja3', 'x-ja4', 'x-ja4h']:
            self._server.Streams.All += Testers.ContainsExpression(
                rf'{header}:', f'Verify {header} header was forwarded.', reflags=re.IGNORECASE)

    def _configure_trafficserver(self) -> None:
        '''Configure Traffic Server with multiple methods.'''
        name = f'ts_all{AllMethodsTest._ts_counter}'
        self._ts = Test.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        AllMethodsTest._ts_counter += 1

        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        server_port = self._server.Variables.https_port

        self._ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.proxy_name': 'test.proxy.test',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'jax_fingerprint',
            })

        # Each of the following pairs makes sure that the expression exists
        # exactly once.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Reserved shared user_arg slot: type=vconn, index=\d+', 'Verify a shared vconn user arg slot was reserved.')
        self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'Reserved shared user_arg slot: type=vconn, index=\d+.*Reserved shared user_arg slot: type=vconn, index=\d+',
            'Verify the shared vconn user arg slot was reserved only once.',
            reflags=re.MULTILINE | re.DOTALL)

        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Reserved shared user_arg slot: type=txn, index=\d+', 'Verify a shared txn user arg slot was reserved.')
        self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'Reserved shared user_arg slot: type=txn, index=\d+.*Reserved shared user_arg slot: type=txn, index=\d+',
            'Verify the shared txn user arg slot was reserved only once.',
            reflags=re.MULTILINE | re.DOTALL)

        # Ensure that JA3 and JA4 share the same vconn user arg slot.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Using shared user_arg: type=vconn, method=JA3, index=(\d+).*Using shared user_arg: type=vconn, method=JA4, index=\1',
            'Verify JA3 and JA4 share the same vconn user arg slot.',
            reflags=re.MULTILINE | re.DOTALL)
        # Note that JA4H is on txn not vconn as JA3 and JA4 above.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Using shared user_arg: type=txn, method=JA4H, index=\d+', 'Verify JA4H uses the shared txn user arg slot.')

        # Load multiple methods - all share the same user arg slot via ContextMap.
        self._ts.Disk.plugin_config.AddLines(
            [
                'jax_fingerprint.so --method JA3 --header x-ja3 --standalone',
                'jax_fingerprint.so --method JA4 --header x-ja4 --standalone',
                'jax_fingerprint.so --method JA4H --header x-ja4h --standalone',
            ])

        self._ts.Disk.remap_config.AddLine(f'map https://jax.server.test https://jax.backend.test:{server_port}')

    def _configure_client(self, tr: 'TestRun') -> None:
        '''Configure the verifier client.'''
        name = f'client_all{AllMethodsTest._client_counter}'
        p = tr.AddVerifierClientProcess(
            name, self._replay_file, http_ports=[self._ts.Variables.port], https_ports=[self._ts.Variables.ssl_port])
        AllMethodsTest._client_counter += 1

        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts


# --- All Methods Test --------------------------------------------------------

# Multiple methods loaded simultaneously, verifying shared context map works.
AllMethodsTest('Multiple methods loaded simultaneously')
