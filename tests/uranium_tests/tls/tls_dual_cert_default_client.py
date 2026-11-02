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
"""Probe default and named certificate selection with restricted signature algorithms."""

import re
import subprocess
import sys


def main(port: int) -> None:
    cases = 0
    for version in ('tls1_2', 'tls1_3'):
        for servername, common_name in ((None, 'foo.com'), ('unknown.example', 'foo.com'), ('one.com', 'group.com')):
            for sigalg, signature in (('rsa_pss_rsae_sha256', r'(RSA-PSS|rsa_pss_)'), ('ecdsa_secp256r1_sha256',
                                                                                       r'(ECDSA|ecdsa_)')):
                command = [
                    'openssl',
                    's_client',
                    '-brief',
                    '-connect',
                    f'127.0.0.1:{port}',
                    f'-{version}',
                    '-sigalgs',
                    sigalg,
                ]
                command += ['-servername', servername] if servername else ['-noservername']
                if version == 'tls1_2':
                    auth = 'RSA' if sigalg.startswith('rsa_') else 'ECDSA'
                    command += ['-cipher', f'ECDHE-{auth}-AES128-GCM-SHA256']
                result = subprocess.run(command, input='', capture_output=True, text=True, timeout=10)
                output = result.stdout + result.stderr
                label = f'{version}, SNI={servername}, sigalg={sigalg}'
                if (result.returncode != 0 or re.search(rf'Signature type: {signature}', output) is None or
                        re.search(rf'Peer certificate:.*CN\s*=\s*{re.escape(common_name)}(?:\s|$)', output) is None):
                    raise RuntimeError(f'{label} failed (exit {result.returncode}):\n{output}')
                print(f'PASS: {label}', flush=True)
                cases += 1
    print(f'PASS: {cases} certificate selections', flush=True)


if __name__ == '__main__':
    main(int(sys.argv[1]))
