'''
Verify reloading the jax_fingerprint plugin configuration at runtime.

Covers traffic_ctl plugin msg jax_fingerprint.reload for configurations
loaded from plugin.config, and traffic_ctl config reload for remap
configuration files.
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

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))
exec(open(os.path.join(Test.TestDirectory, 'jax_fingerprint_common.py')).read())


class ReloadTest:
    '''Verify that traffic_ctl plugin msg jax_fingerprint.reload re-reads the configuration.

    The servernames allowlist is switched at runtime from jax.server.test to
    jax-filtered.server.test, and a reload that changes a startup-only
    setting is rejected while the current configuration stays in effect.
    A second plugin.config line with its own configuration file verifies
    that each line reloads its own file independently.
    '''

    _replay_file: str = 'jax_fingerprint_reload.replay.yaml'
    _config_name: str = 'jax_fingerprint.yaml'
    _second_config_name: str = 'jax_fingerprint_ja4h.yaml'
    _client_counter: int = 0

    def __init__(self, name: str) -> None:
        '''Configure the reload test runs.'''
        self._name = name
        tr = Test.AddTestRun(f'{name}: initial servernames')
        self._dns = Test.MakeDNServer('dns_reload', default='127.0.0.1')
        self._server = Test.MakeVerifierServerProcess('server_reload', self._replay_file)
        self._configure_trafficserver()
        self._add_client_run(tr, 'initial-allowed initial-filtered', start_processes=True)

        self._add_reload_run(f'{name}: reload new servernames', 'jax_fingerprint_reloaded.yaml')
        Test.AddAwaitFileContainsTestRun(
            f'{name}: await the reload', self._ts.Disk.diags_log.AbsPath,
            f'Configuration reloaded successfully from {self._config_name}')
        self._add_client_run(Test.AddTestRun(f'{name}: reloaded servernames'), 'reloaded-allowed reloaded-filtered')

        self._add_reload_run(f'{name}: reload a startup-only change', 'jax_fingerprint_incompatible.yaml')
        Test.AddAwaitFileContainsTestRun(
            f'{name}: await the rejected reload', self._ts.Disk.diags_log.AbsPath,
            f'Configuration reload from {self._config_name} rejected')
        self._add_client_run(Test.AddTestRun(f'{name}: servernames kept after rejection'), 'reloaded-allowed reloaded-filtered')

    def _configure_trafficserver(self) -> None:
        '''Configure Traffic Server with the initial and the reloaded configurations.'''
        self._ts = Test.MakeATSProcess('ts_reload', enable_cache=False, enable_tls=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
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

        def fingerprint(method: str, servername: str) -> dict[str, str | bool | list[str]]:
            return {
                'method': method,
                'standalone': True,
                'header': 'x-jax',
                'via_header': 'x-jax-via',
                'servernames': [servername],
            }

        self._ts.Disk.MakeConfigFile(self._config_name).update(make_config([fingerprint('JA4', 'jax.server.test')]))
        self._ts.Disk.MakeConfigFile('jax_fingerprint_reloaded.yaml').update(
            make_config([fingerprint('JA4', 'jax-filtered.server.test')]))
        self._ts.Disk.MakeConfigFile('jax_fingerprint_incompatible.yaml').update(
            make_config([fingerprint('JA3', 'jax.server.test')]))
        self._ts.Disk.MakeConfigFile(self._second_config_name).update(
            make_config([{
                'method': 'JA4H',
                'standalone': True,
                'header': 'x-ja4h'
            }]))
        self._ts.Disk.plugin_config.AddLines(
            [
                f'jax_fingerprint.so {self._config_name}',
                f'jax_fingerprint.so {self._second_config_name}',
            ])

        server_port = self._server.Variables.https_port
        for host in ('jax.server.test', 'jax-filtered.server.test'):
            self._ts.Disk.remap_config.AddLine(f'map https://{host} https://jax.backend.test:{server_port}')

        # Replace the default "no errors" check since the rejected reload is logged as an error.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"Configuration reload from jax_fingerprint\.yaml rejected: 'method' of fingerprint entry 1 changed",
            'Verify the reload that changes a startup-only setting is rejected.')
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Configuration reloaded successfully from jax_fingerprint_ja4h\.yaml',
            'Verify the second plugin.config line reloads its own configuration.')

    def _add_reload_run(self, name: str, config_name: str) -> None:
        '''Install a configuration file and ask the plugin to reload it.'''
        config_dir = self._ts.Variables.CONFIGDIR
        tr = Test.AddTestRun(name)
        tr.Processes.Default.Command = (
            f'cp {config_dir}/{config_name} {config_dir}/{self._config_name} && '
            'traffic_ctl plugin msg jax_fingerprint.reload')
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts

    def _add_client_run(self, tr: 'TestRun', keys: str, start_processes: bool = False) -> None:
        '''Run the replay sessions selected by keys.'''
        p = tr.AddVerifierClientProcess(
            f'client_reload{ReloadTest._client_counter}',
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            https_ports=[self._ts.Variables.ssl_port],
            keys=keys)
        if start_processes:
            p.StartBefore(self._dns)
            p.StartBefore(self._server)
            p.StartBefore(self._ts)
        ReloadTest._client_counter += 1
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


ReloadTest('Reload servernames')


class RemapReloadTest:
    '''Verify that editing only a remap configuration file and running traffic_ctl config reload reloads it.'''

    _replay_file: str = 'jax_fingerprint_remap_reload.replay.yaml'
    _config_name: str = 'jax_fingerprint_remap.yaml'

    def __init__(self, name: str) -> None:
        '''Configure the remap reload test runs.'''
        self._dns = Test.MakeDNServer('dns_remap_reload', default='127.0.0.1')
        self._server = Test.MakeVerifierServerProcess('server_remap_reload', self._replay_file)
        self._configure_trafficserver()

        tr = Test.AddTestRun(f'{name}: initial header')
        client = self._add_client(tr, 'client_remap_reload0', 'remap-initial')
        client.StartBefore(self._dns)
        client.StartBefore(self._server)
        client.StartBefore(self._ts)

        config_dir = self._ts.Variables.CONFIGDIR
        traffic_out = self._ts.Disk.traffic_out.AbsPath
        loading = f'Loading configuration from {config_dir}/{self._config_name}'
        tr = Test.AddTestRun(f'{name}: change only the remap configuration and reload')
        tr.Processes.Default.Command = (
            f'cp {config_dir}/jax_fingerprint_remap_reloaded.yaml {config_dir}/{self._config_name} && '
            'traffic_ctl config reload && '
            f'for i in $$(seq 60); do [ "$$(grep -c "{loading}" {traffic_out})" -ge 2 ] && exit 0; sleep 1; done; exit 1')
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts

        self._add_client(Test.AddTestRun(f'{name}: reloaded header'), 'client_remap_reload1', 'remap-reloaded')

    def _configure_trafficserver(self) -> None:
        '''Configure Traffic Server with a remap rule that uses the plugin.'''
        self._ts = Test.MakeATSProcess('ts_remap_reload', enable_cache=False)
        self._ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'jax_fingerprint',
            })

        def fingerprint(header: str) -> dict[str, str | bool | list[str]]:
            return {'method': 'JA4H', 'header': header}

        self._ts.Disk.MakeConfigFile(self._config_name).update(make_config([fingerprint('x-jax-initial')]))
        self._ts.Disk.MakeConfigFile('jax_fingerprint_remap_reloaded.yaml').update(make_config([fingerprint('x-jax-reloaded')]))
        self._ts.Disk.remap_config.AddLine(
            f'map http://jax.server.test http://jax.backend.test:{self._server.Variables.http_port} '
            f'@plugin=jax_fingerprint.so @pparam={self._config_name}')

    def _add_client(self, tr: 'TestRun', name: str, keys: str) -> 'Process':
        '''Run the replay session selected by keys.'''
        client = tr.AddVerifierClientProcess(name, self._replay_file, http_ports=[self._ts.Variables.port], keys=keys)
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server
        return client


RemapReloadTest('Reload a remap configuration')
