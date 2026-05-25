'''
Verify the legacy ssl_multicert.config fallback path.
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
from enum import Enum

Test.Summary = '''
Verify Apache Traffic Server falls back to legacy ssl_multicert.config
when ssl_multicert.yaml is absent.
'''

SNI_DOMAIN = 'example.com'
REPLAY_FILE = 'replay/ssl_multicert_legacy_fallback.replay.yaml'


class _BaseSslMulticertTest:
    """
    Common scaffolding: a single ATS instance + verifier origin server.
    Subclasses override _setupTS to stage the scenario-specific config.
    """

    _client_counter = 0

    class State(Enum):
        INIT = 0
        RUNNING = 1

    def __init__(self, ts_name, server_name):
        self.state = self.State.INIT
        self.ts_name = ts_name
        self.server_name = server_name
        self._setupServer()
        self._setupTS()

    def _setupServer(self):
        self.server = Test.MakeVerifierServerProcess(self.server_name, REPLAY_FILE)

    def _setupTS(self):
        raise NotImplementedError

    def _baseRecordsConfig(self):
        return {
            'proxy.config.ssl.server.cert.path': f'{self.ts.Variables.SSLDir}',
            'proxy.config.ssl.server.private_key.path': f'{self.ts.Variables.SSLDir}',
        }

    def _checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.ts
            tr.StillRunningBefore = self.server
        else:
            tr.Processes.Default.StartBefore(self.ts)
            tr.Processes.Default.StartBefore(self.server)
            self.state = self.State.RUNNING

    def _checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

    def _addReplayRun(self, description):
        tr = Test.AddTestRun(description)
        self._checkProcessBefore(tr)
        client_name = f'client-{_BaseSslMulticertTest._client_counter}'
        _BaseSslMulticertTest._client_counter += 1
        tr.AddVerifierClientProcess(client_name, REPLAY_FILE, https_ports=[self.ts.Variables.ssl_port])
        self._checkProcessAfter(tr)

    def _addRegistryRun(self, description, expected_files):
        """
        traffic_ctl config registry should list the SSL multicert files
        registered by the SSL client coordinator at startup.
        """
        tr = Test.AddTestRun(description)
        self._checkProcessBefore(tr)
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.Command = 'traffic_ctl config registry'
        tr.Processes.Default.ReturnCode = 0
        for fname in expected_files:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                fname.replace('.', r'\.'), f'{fname} should appear in the file registry')
        self._checkProcessAfter(tr)


class LegacySslMulticertOnlyTest(_BaseSslMulticertTest):
    """
    Only legacy ssl_multicert.config is staged; ATS should fall back to it
    and load TLS certs successfully.
    """

    def __init__(self):
        super().__init__("ts", "server")

    def _setupTS(self):
        self.ts = Test.MakeATSProcess(self.ts_name, enable_tls=True, use_legacy_ssl_multicert=True)
        self.ts.Disk.records_config.update(self._baseRecordsConfig())
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.http_port}')

        self.ts.Setup.CopyAs(os.path.join(Test.TestDirectory, 'etc', 'ssl_multicert.config'), self.ts.Variables.CONFIGDIR)

        self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'ssl_multicert\.yaml not found, falling back to ssl_multicert\.config',
            'ssl_multicert.yaml fallback Note should be logged when only the legacy file is present')

    def run(self):
        self._addReplayRun(f"Connect using cert loaded from legacy ssl_multicert.config (SNI={SNI_DOMAIN})")
        self._addRegistryRun(
            "Verify config registry lists ssl_multicert.yaml and legacy ssl_multicert.config",
            ['ssl_multicert.yaml', 'ssl_multicert.config'])


class CoexistSslMulticertTest(_BaseSslMulticertTest):
    """
    Both ssl_multicert.yaml and ssl_multicert.config exist; YAML wins and a
    Note about the legacy file being ignored is logged.
    """

    def __init__(self):
        super().__init__("ts2", "server2")

    def _setupTS(self):
        self.ts = Test.MakeATSProcess(self.ts_name, enable_tls=True)
        self.ts.Disk.records_config.update(self._baseRecordsConfig())
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.http_port}')

        self.ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        self.ts.Setup.CopyAs(os.path.join(Test.TestDirectory, 'etc', 'ssl_multicert.config'), self.ts.Variables.CONFIGDIR)

        self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'ssl_multicert\.config exists alongside ssl_multicert\.yaml; the legacy file is ignored',
            'Note about ignored legacy ssl_multicert.config should be logged when both files are present')

    def run(self):
        self._addReplayRun(f"yaml wins when both files exist (SNI={SNI_DOMAIN})")
        self._addRegistryRun(
            "Verify config registry lists both ssl_multicert.yaml and ssl_multicert.config",
            ['ssl_multicert.yaml', 'ssl_multicert.config'])


class CustomFilenameSslMulticertTest(_BaseSslMulticertTest):
    """
    proxy.config.ssl.server.multicert.filename is set to a non-default value.
    The legacy fallback must NOT engage even if ssl_multicert.config exists.
    """

    def __init__(self):
        super().__init__("ts3", "server3")

    def _setupTS(self):
        self.ts = Test.MakeATSProcess(self.ts_name, enable_tls=True, use_legacy_ssl_multicert=True)
        records = self._baseRecordsConfig()
        records['proxy.config.ssl.server.multicert.filename'] = 'custom_multicert.yaml'
        self.ts.Disk.records_config.update(records)
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.http_port}')

        custom_path = os.path.join(self.ts.Variables.CONFIGDIR, 'custom_multicert.yaml')
        self.ts.Disk.File(custom_path, id='ssl_multicert_yaml_custom', typename='ats:config')
        self.ts.Disk.ssl_multicert_yaml_custom.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        self.ts.Setup.CopyAs(os.path.join(Test.TestDirectory, 'etc', 'ssl_multicert.config'), self.ts.Variables.CONFIGDIR)

        self.ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'ssl_multicert\.yaml not found, falling back to ssl_multicert\.config',
            'Fallback Note must NOT appear when record is at a non-default filename')
        self.ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'ssl_multicert\.config exists alongside ssl_multicert\.yaml; the legacy file is ignored',
            '"Both present" Note must NOT appear when record is at a non-default filename')

    def run(self):
        self._addReplayRun(f"Custom record value disables legacy fallback (SNI={SNI_DOMAIN})")
        self._addRegistryRun(
            "Verify config registry lists custom_multicert.yaml and legacy ssl_multicert.config",
            ['custom_multicert.yaml', 'ssl_multicert.config'])


LegacySslMulticertOnlyTest().run()
CoexistSslMulticertTest().run()
CustomFilenameSslMulticertTest().run()
