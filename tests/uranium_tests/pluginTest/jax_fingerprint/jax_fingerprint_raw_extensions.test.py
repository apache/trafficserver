'''
Verify that jax_fingerprint JA4 counts extensions OpenSSL does not recognize.

OpenSSL's ClientHello API omits GREASE and extension types it does not
recognize, such as ALPS. JA4 excludes GREASE by design, but it must still
count and hash ALPS.
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

import sys

sys.path.insert(0, Test.TestDirectory)
from chrome_client_hello import expected_ja4

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))

# Wireshark's JA4 for this Chrome ClientHello: 15 non-GREASE ciphers and 16
# non-GREASE extensions, including ALPS.
CHROME_JA4 = 't13d1516h2_8daaf6152771_e5627efa2ab1'
assert expected_ja4() == CHROME_JA4, expected_ja4()

ts = Test.MakeATSProcess('ts', enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_yaml.AddLines(
    '''
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
'''.split('\n'))
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'jax_fingerprint',
    })
ts.Disk.plugin_config.AddLine('jax_fingerprint.so --method JA4')
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f'Fingerprint: {CHROME_JA4}', 'JA4 should count the ALPS extension, which OpenSSL does not recognize.')

tr = Test.AddTestRun('Send a Chrome ClientHello')
tr.Setup.Copy('chrome_client_hello.py')
tr.Processes.Default.Command = f'{sys.executable} chrome_client_hello.py {ts.Variables.ssl_port}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    f'Expected JA4: {CHROME_JA4}', 'The client should get a ServerHello.')
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

Test.AddAwaitFileContainsTestRun('Await the JA4 fingerprint', ts.Disk.traffic_out.AbsPath, 'Fingerprint: ')
