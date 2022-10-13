'''
Test TLS secrets logging.
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


Test.Summary = '''
Test TLS secrets logging.
'''


class TlsKeyloggingTest:

    replay_file = "tls_session_key_logging.replay.yaml"

    server_counter = 0
    ts_counter = 0
    client_counter = 0

    def __init__(self, enable_secrets_logging):
        self.setupOriginServer()
        self.setupTS(enable_secrets_logging)

    def setupOriginServer(self):
        server_name = f"server_{TlsKeyloggingTest.server_counter}"
        TlsKeyloggingTest.server_counter += 1
        self.server = Test.MakeVerifierServerProcess(
            server_name, TlsKeyloggingTest.replay_file)

    def setupTS(self, enable_secrets_logging):
        ts_name = f"ts_{TlsKeyloggingTest.ts_counter}"
        TlsKeyloggingTest.ts_counter += 1
        self.ts = Test.MakeATSProcess(ts_name, enable_tls=True, enable_cache=False)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.records_config.update({
            "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
            "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
            "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',

            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl_keylog'
        })
        self.ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )
        self.ts.Disk.remap_config.AddLine(
            f'map / https://127.0.0.1:{self.server.Variables.https_port}'
        )

        keylog_file = os.path.join(self.ts.Variables.LOGDIR, "tls_secrets.txt")

        # Remove the keylog_file configuration automatically configured via the
        # trafficserver AuTest extension.
        del self.ts.Disk.records_config['proxy.config.ssl.keylog_file']

        if enable_secrets_logging:
            self.ts.Disk.records_config.update({
                'proxy.config.ssl.keylog_file': keylog_file,
            })

            self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
                f"Opened {keylog_file} for TLS key logging",
                "Verify the user was notified of TLS secrets logging.")
            self.ts.Disk.File(keylog_file, id="keylog", exists=True)
            # It would be nice to verify the content of certain lines in the
            # keylog file, but the content is dependent upon the particular TLS
            # protocol version. Thus I'm hesitant to add ContainsExpression
            # checks here which will be fragile and eventually become outdated.
        else:
            self.ts.Disk.File(keylog_file, exists=False)

    def run(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)

        client_name = f"client_{TlsKeyloggingTest.client_counter}"
        TlsKeyloggingTest.client_counter += 1
        tr.AddVerifierClientProcess(
            client_name,
            TlsKeyloggingTest.replay_file,
            https_ports=[self.ts.Variables.ssl_port])


TlsKeyloggingTest(enable_secrets_logging=False).run()
TlsKeyloggingTest(enable_secrets_logging=True).run()
