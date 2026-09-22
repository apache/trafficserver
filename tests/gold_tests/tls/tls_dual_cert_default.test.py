#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Select default RSA and EC certificates independently of their order."""

import os
import shlex
import sys

Test.Summary = __doc__
Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))

for order in ('rsa_ec', 'ec_rsa'):
    tr = Test.ATSReplayTest(replay_file=f'replay/dual_cert_default_{order}.replay.yaml')
    ts = tr.Processes[f'ts_{order}']
    for name in ('signed-foo', 'signed-foo-ec', 'signed-san', 'signed-san-ec'):
        ts.addSSLfile(f'ssl/{name}.pem')
        ts.addSSLfile(f'ssl/{name}.key')
    ts.Disk.records_config.update(
        {
            'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
            'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        })
    # Proxy Verifier checks the HTTP exchange. Use s_client afterward to constrain
    # signature algorithms, which the replay client cannot configure.
    helper = shlex.quote(os.path.join(Test.TestDirectory, 'tls_dual_cert_default_client.py'))
    tr.Processes.Default.Command += f' && {shlex.quote(sys.executable)} {helper} {ts.Variables.ssl_port}'
    tr.Processes.Default.Streams.All += Testers.ContainsExpression(
        'PASS: 12 certificate selections', f'Both key types work with certificate order {order}')
