'''
Verify that jax_fingerprint's JA4 fingerprint reflects the signature
algorithms of a live ClientHello, in the order the client sent them.
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

# The connections differ only in -sigalgs: the same two algorithms in both
# orders, then one more. Every list includes an algorithm the RSA test
# certificate can sign with so that the handshake and request complete.
SIGALGS = [
    'rsa_pss_rsae_sha256:ecdsa_secp256r1_sha256',
    'ecdsa_secp256r1_sha256:rsa_pss_rsae_sha256',
    'rsa_pss_rsae_sha256:ecdsa_secp256r1_sha256:rsa_pss_rsae_sha384',
]

ts = Test.MakeATSProcess('ts', enable_cache=False, enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'jax_fingerprint',
        'proxy.config.log.max_secs_per_buffer': 1,
    })
# The fingerprint is logged when the request header is read, before remap, so
# no remap rule or origin is needed.
ts.Disk.plugin_config.AddLine('jax_fingerprint.so --standalone --method JA4 --log-filename jax_fingerprint')

log_path = os.path.join(ts.Variables.LOGDIR, 'jax_fingerprint.log')
ts.Disk.File(log_path, id='jax_log')
# One line per connection, in order: a and b must be identical, and each c must
# differ from every earlier one.
ts.Disk.jax_log.Content += Testers.ContainsExpression(
    r'JA4: (?P<ab>[a-z0-9]{10}_[0-9a-f]{12})_(?P<c1>[0-9a-f]{12})\n'
    r'[^\n]*JA4: (?P=ab)_(?!(?P=c1)\n)(?P<c2>[0-9a-f]{12})\n'
    r'[^\n]*JA4: (?P=ab)_(?!(?P=c1)\n)(?!(?P=c2)\n)[0-9a-f]{12}\n',
    'Verify only the c section changes with the signature algorithms and their order.',
    reflags=re.MULTILINE)

for i, sigalgs in enumerate(SIGALGS):
    tr = Test.AddTestRun(f'Connect with -sigalgs {sigalgs}')
    tr.Processes.Default.Command = (
        "printf 'GET / HTTP/1.1\\r\\nHost: jax.server.test\\r\\nConnection: close\\r\\n\\r\\n' | "
        f"openssl s_client -connect 127.0.0.1:{ts.Variables.ssl_port} -servername jax.server.test "
        f"-sigalgs '{sigalgs}' -ign_eof")
    tr.ReturnCode = 0
    tr.Processes.Default.Streams.All += Testers.ContainsExpression(
        r'HTTP/1\.1 \d{3}', 'Verify the request reached Traffic Server over TLS.')
    if i == 0:
        tr.Processes.Default.StartBefore(ts)
    tr.StillRunningAfter = ts

Test.AddAwaitFileContainsTestRun(
    'Await a fingerprint for every connection', ts.Disk.jax_log.AbsPath, 'JA4: ', desired_count=len(SIGALGS))
