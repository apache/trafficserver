'''
Shared helpers and test classes for the jax_fingerprint AuTests.

Each jax_fingerprint test file executes this file in its own namespace so
that these classes can use the AuTest globals (Test, Testers, and so on).
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


def make_config(fingerprints: list[dict[str, str | bool | list[str]]]) -> dict:
    '''Build a jax_fingerprint YAML configuration.

    :param fingerprints: One dict of fingerprint settings per list entry.
    :return: The configuration content.
    '''
    return {'jax_fingerprint': {'fingerprints': fingerprints}}


def make_command_line(fingerprint: dict[str, str | bool | list[str]]) -> list[str]:
    '''Build the command-line options equivalent to a fingerprint configuration entry.

    :param fingerprint: The fingerprint settings, keyed as in the configuration file.
    :return: One --option=value string per setting.
    '''
    options = []
    for key, value in fingerprint.items():
        option = '--' + key.replace('_', '-')
        if isinstance(value, bool):
            if value:
                options.append(option)
        elif isinstance(value, list):
            options.append(f'{option}={",".join(value)}')
        else:
            options.append(f'{option}={value}')
    return options


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
            log_field: str = '',
            cli: bool = False) -> None:
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
            servernames to the plugin.  Connections whose SNI is not in
            the list are skipped entirely: handle_client_hello returns early,
            no context is created, and handle_read_request_hdr is a no-op.
            Only meaningful for CONNECTION_BASED methods (JA3/JA4) in global
            setup.
        :param log_field: Symbol name for the log_field setting.  When set,
            configures logging.yaml with a custom format using the symbol
            and verifies the fingerprint appears in the ATS access log.
            Only supported with global setup.
        :param cli: If True, configure the plugin with command-line options
            instead of a configuration file.

        Method notes:
          - JA3 / JA4 are CONNECTION_BASED (triggered on TLS client hello)
            and require TLS between the client and ATS.
          - JA4H is REQUEST_BASED (triggered on HTTP request read) and
            works over plain HTTP, HTTPS, and HTTP/2.

        Setup notes:
          - global:  plugin.config entry whose configuration sets
            standalone so ATS registers the READ_REQUEST_HDR hook and
            modifies every request.
          - remap:   @plugin in remap.config.  For CONNECTION_BASED methods
            standalone must also be set so the remap plugin registers
            the SSL_CLIENT_HELLO_HOOK globally and can populate the vconn
            context that TSRemapDoRemap reads later.
          - hybrid:  global plugin (not standalone) captures the TLS client
            hello and stores context on the vconn; a remap plugin (not
            standalone) reads that shared context and sets headers only
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
        self._cli = cli
        # HTTP/2 always runs over TLS (h2 requires TLS).
        self._needs_tls = method in ('JA3', 'JA4') or http2
        self._replay_file = self._choose_replay_file()
        self._fingerprint_pattern = rf'{self._method}: [a-z0-9_-]+'

        tr = Test.AddTestRun(name)
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)
        Test.AddAwaitFileContainsTestRun(
            f'Await jax_fingerprint.log for: {self._name}', self._ts.Disk.jax_log.AbsPath, self._fingerprint_pattern)

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
        '''Write the remap plugin configuration and return the @plugin fragment for a remap.config line.'''
        fingerprint: dict[str, str | bool | list[str]] = {
            'method': self._method,
            'header': 'x-jax',
            'via_header': 'x-jax-via',
            'log_filename': 'jax_fingerprint',
        }
        if self._mode != 'overwrite':
            fingerprint['mode'] = self._mode
        if add_standalone:
            fingerprint['standalone'] = True
        if self._cli:
            return ' '.join(['@plugin=jax_fingerprint.so'] + [f'@pparam={option}' for option in make_command_line(fingerprint)])
        config_name = 'jax_fingerprint_remap.yaml'
        self._ts.Disk.MakeConfigFile(config_name).update(make_config([fingerprint]))
        return f'@plugin=jax_fingerprint.so @pparam={config_name}'

    def _add_global_plugin(self, fingerprint: dict[str, str | bool | list[str]]) -> None:
        '''Write the global plugin configuration and load it via plugin.config.'''
        if self._cli:
            self._ts.Disk.plugin_config.AddLine(' '.join(['jax_fingerprint.so'] + make_command_line(fingerprint)))
            return
        config_name = 'jax_fingerprint.yaml'
        self._ts.Disk.MakeConfigFile(config_name).update(make_config([fingerprint]))
        self._ts.Disk.plugin_config.AddLine(f'jax_fingerprint.so {config_name}')

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
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        log_path = os.path.join(self._ts.Variables.LOGDIR, 'jax_fingerprint.log')
        self._ts.Disk.File(log_path, id='jax_log')
        self._ts.Disk.jax_log.Content += Testers.ContainsExpression(
            self._fingerprint_pattern, f'Verify the jax_fingerprint log contains a {self._method} fingerprint.')

        backend = f'{scheme}://jax.backend.test:{server_port}'
        backend_no_plugin = f'{scheme}://jax.backend.test:{server_port}'

        if self._setup == 'global':
            fingerprint: dict[str, str | bool | list[str]] = {
                'method': self._method,
                'standalone': True,
                'header': 'x-jax',
                'via_header': 'x-jax-via',
                'log_filename': 'jax_fingerprint',
            }
            if self._mode != 'overwrite':
                fingerprint['mode'] = self._mode
            if self._servernames:
                fingerprint['servernames'] = self._servernames.split(',')
            if self._log_field:
                fingerprint['log_field'] = self._log_field
            self._add_global_plugin(fingerprint)
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax.server.test {backend}')
            if self._servernames:
                # Second remap rule for the SNI that is NOT in the allowlist.
                self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax-filtered.server.test {backend}')

        elif self._setup == 'remap':
            # Route without plugin (session 1 in replay file)
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax-no-plugin.server.test {backend_no_plugin}')
            # Route with plugin (session 2 in replay file)
            # CONNECTION_BASED methods need standalone so the remap plugin
            # registers the SSL_CLIENT_HELLO_HOOK to populate the vconn context.
            remap_line = self._build_remap_plugin_line(add_standalone=self._needs_tls)
            self._ts.Disk.remap_config.AddLine(f'map {scheme}://jax.server.test {backend} {remap_line}')

        elif self._setup == 'hybrid':
            # Global plugin: registers SSL_CLIENT_HELLO_HOOK to capture the
            # TLS handshake and store context on the vconn.  Without standalone
            # there is no READ_REQUEST_HDR hook, so headers are never set here.
            fingerprint: dict[str, str | bool | list[str]] = {'method': self._method}
            if self._servernames:
                fingerprint['servernames'] = self._servernames.split(',')
            self._add_global_plugin(fingerprint)
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


class AllMethodsTest:
    '''Test multiple fingerprint methods loaded simultaneously.

    When a configuration lists multiple fingerprints, they share user arg
    slots via a ContextMap. This test verifies that JA3/JA4 and JA4H can
    coexist and produce fingerprints while sharing that context machinery
    across their respective vconn and txn storage.
    '''

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(self, name: str, cli: bool = False) -> None:
        '''Configure test with multiple methods loaded.

        :param name: Descriptive name for this test run.
        :param cli: If True, load the plugin once per method with command-line
            options instead of once with a configuration file.
        '''
        self._name = name
        self._cli = cli
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
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        # Each of the following pairs makes sure that the expression exists
        # exactly once.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Reserved shared user_arg slot: type=vconn, name=test.jax.registry, index=\d+',
            'Verify a shared vconn user arg slot was reserved.')
        self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'Reserved shared user_arg slot: type=vconn, name=test.jax.registry, index=\d+.*'
            r'Reserved shared user_arg slot: type=vconn, name=test.jax.registry, index=\d+',
            'Verify the shared vconn user arg slot was reserved only once.',
            reflags=re.MULTILINE | re.DOTALL)

        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Reserved shared user_arg slot: type=txn, name=test.jax.registry, index=\d+',
            'Verify a shared txn user arg slot was reserved.')
        self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'Reserved shared user_arg slot: type=txn, name=test.jax.registry, index=\d+.*'
            r'Reserved shared user_arg slot: type=txn, name=test.jax.registry, index=\d+',
            'Verify the shared txn user arg slot was reserved only once.',
            reflags=re.MULTILINE | re.DOTALL)

        # Ensure that JA3 and JA4 share the same vconn user arg slot.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Using shared user_arg: type=vconn, name=test.jax.registry, method=JA3, index=(\d+).*'
            r'Using shared user_arg: type=vconn, name=test.jax.registry, method=JA4, index=\1',
            'Verify JA3 and JA4 share the same vconn user arg slot.',
            reflags=re.MULTILINE | re.DOTALL)
        # Note that JA4H is on txn not vconn as JA3 and JA4 above.
        self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'Using shared user_arg: type=txn, name=test.jax.registry, method=JA4H, index=\d+',
            'Verify JA4H uses the shared txn user arg slot.')

        # Configure multiple methods - all share the same user arg slot via ContextMap.
        fingerprints: list[dict[str, str | bool | list[str]]] = [
            {
                'method': method,
                'header': f'x-{method.lower()}',
                'standalone': True,
                'export': 'test.jax.registry'
            } for method in ('JA3', 'JA4', 'JA4H')
        ]
        if self._cli:
            self._ts.Disk.plugin_config.AddLines(
                [' '.join(['jax_fingerprint.so'] + make_command_line(fingerprint)) for fingerprint in fingerprints])
        else:
            self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                'multiple loading of plugin', 'Verify jax_fingerprint is loaded only once.')
            config_name = 'jax_fingerprint.yaml'
            self._ts.Disk.MakeConfigFile(config_name).update(make_config(fingerprints))
            self._ts.Disk.plugin_config.AddLine(f'jax_fingerprint.so {config_name}')

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
