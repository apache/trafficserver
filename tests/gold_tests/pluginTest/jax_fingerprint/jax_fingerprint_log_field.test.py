'''
Verify that jax_fingerprint --log-field logs connection fingerprints after the client connection closes.

ATS can close the client connection before it writes the transaction log entry,
for example when the client sends Connection: close or when the client times out
while sending its request header. The log field must still report the
fingerprint, and must not crash ATS, in those cases.
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
import sys

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))


class JaxLogFieldClosedConnectionTest:
    '''Exercise --log-field for transactions whose client connection closes before logging.'''

    _replay_file: str = 'jax_fingerprint_log_field.replay.yaml'
    _client_script: str = 'tls_partial_client.py'
    _sni: str = 'jax.server.test'
    _ja3_pattern: str = r'ja3=[0-9a-f]{32}'
    _ja4_pattern: str = r'ja4=t13[0-9a-z_]+'
    _ja4h_pattern: str = r'ja4h=ge[0-9a-z_]{9,}'

    def __init__(self) -> None:
        self._dns = Test.MakeDNServer('dns', default='127.0.0.1')
        self._server = Test.MakeVerifierServerProcess('server', self._replay_file)
        self._configure_trafficserver()
        self._log_path = os.path.join(self._ts.Variables.LOGDIR, 'jax_log_field.log')

        self._add_connection_close_run()
        self._add_partial_request_run()
        self._add_handshake_only_run()
        self._add_log_verification_runs()

    def _configure_trafficserver(self) -> None:
        '''Configure ATS with connection-based fingerprint log fields.'''
        self._ts = Test.MakeATSProcess('ts', enable_cache=False, enable_tls=True)
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
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'jax_fingerprint|http',
                'proxy.config.log.max_secs_per_buffer': 1,
                # Time out stalled clients quickly so the partial request and
                # handshake-only connections close within the test.
                'proxy.config.http.transaction_no_activity_timeout_in': 2,
                'proxy.config.http.accept_no_activity_timeout': 2,
            })

        self._ts.Disk.plugin_config.AddLines(
            [
                'jax_fingerprint.so --method JA3 --header x-ja3 --log-field jaxja3 --standalone',
                'jax_fingerprint.so --method JA4 --header x-ja4 --log-field jaxja4 --standalone',
                'jax_fingerprint.so --method JA4H --header x-ja4h --log-field jaxja4h --standalone',
            ])
        self._ts.Disk.remap_config.AddLine(f'map https://{self._sni} https://jax.backend.test:{self._server.Variables.https_port}')

        self._ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: jax_custom
      format: 'path=%<cqup> crc=%<crc> ja3=%<jaxja3> ja4=%<jaxja4> ja4h=%<jaxja4h>'
  logs:
    - filename: jax_log_field
      format: jax_custom
'''.split("\n"))

        self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'failed assertion', 'Verify that logging the fingerprint fields did not trip an assertion.')

    def _add_connection_close_run(self) -> None:
        '''Send a complete request with Connection: close.'''
        tr = Test.AddTestRun('Log fingerprints for a Connection: close transaction')
        p = tr.AddVerifierClientProcess(
            'client', self._replay_file, http_ports=[self._ts.Variables.port], https_ports=[self._ts.Variables.ssl_port])
        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

    def _add_client_script_run(self, description: str, extra_args: str) -> 'TestRun':
        tr = Test.AddTestRun(description)
        tr.Setup.CopyAs(os.path.join(Test.TestDirectory, self._client_script), tr.RunDirectory)
        tr.Processes.Default.Command = (
            f'{sys.executable} {self._client_script} {self._ts.Variables.ssl_port} {self._sni} {extra_args}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'ATS closed the connection', 'Verify that ATS timed out and closed the connection.')
        tr.StillRunningAfter = self._ts
        return tr

    def _add_partial_request_run(self) -> None:
        '''Complete the TLS handshake but never finish the request header.'''
        self._add_client_script_run('Log fingerprints for a request header that never completes', '--path /partial')

    def _add_handshake_only_run(self) -> None:
        '''Complete the TLS handshake but send no request bytes at all.

        ATS does not start a transaction until request bytes arrive, so this
        connection produces no transaction log entry. It must not crash ATS.
        '''
        self._add_client_script_run('Complete a TLS handshake without sending a request', '')

    def _add_log_verification_runs(self) -> None:
        '''Verify that the log entries carry real fingerprints, not "-".

        The incomplete request has no JA4H fingerprint because JA4H is computed from the complete request header.
        '''
        fingerprints = f'{self._ja3_pattern} {self._ja4_pattern}'
        Test.AddAwaitFileContainsTestRun(
            'Await the Connection: close log entry', self._log_path,
            f'path=connection-close crc=\\S+ {fingerprints} {self._ja4h_pattern}')
        Test.AddAwaitFileContainsTestRun(
            'Await the incomplete request log entry', self._log_path, f'path=partial crc=ERR_CLIENT_ABORT {fingerprints}')

        self._ts.Disk.File(self._log_path, id='jax_log_field')
        self._ts.Disk.jax_log_field.Content += Testers.ExcludesExpression(
            'ja[34]=-', 'Verify that every logged transaction carries its connection fingerprints.')


JaxLogFieldClosedConnectionTest()
