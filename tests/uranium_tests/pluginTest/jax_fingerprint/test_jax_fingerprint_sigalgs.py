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

import re
import shutil

import pytest

from tools.uranium.services import ATSFactory, wait_for_file_lines


def test_jax_fingerprint_sigalgs(ats_factory: ATSFactory) -> None:
    """Only JA4's c section changes with signature algorithms and their order.

    :param ats_factory: Factory owning the TLS server.
    """
    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    ats = ats_factory.create("ts", enable_tls=True)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "jax_fingerprint",
            "proxy.config.log.max_secs_per_buffer": 1
        })
    ats.plugin_config.add_line("jax_fingerprint.so --standalone --method JA4 --log-filename jax_fingerprint")
    ats.start()
    for sigalgs in ("rsa_pss_rsae_sha256:ecdsa_secp256r1_sha256", "ecdsa_secp256r1_sha256:rsa_pss_rsae_sha256",
                    "rsa_pss_rsae_sha256:ecdsa_secp256r1_sha256:rsa_pss_rsae_sha384"):
        result = ats.run_shell(
            "printf 'GET / HTTP/1.1\\r\\nHost: jax.server.test\\r\\nConnection: close\\r\\n\\r\\n' | "
            f"openssl s_client -connect 127.0.0.1:{ats.https_port} -servername jax.server.test "
            f"-sigalgs '{sigalgs}' -ign_eof")
        assert result.returncode == 0, result.output
        assert re.search(r"HTTP/1\.1 \d{3}", result.output), result.output
    content = wait_for_file_lines(ats.log_directory / "jax_fingerprint.log", "JA4: ", 3)
    assert re.search(
        r"JA4: (?P<ab>[a-z0-9]{10}_[0-9a-f]{12})_(?P<c1>[0-9a-f]{12})\n"
        r"[^\n]*JA4: (?P=ab)_(?!(?P=c1)\n)(?P<c2>[0-9a-f]{12})\n"
        r"[^\n]*JA4: (?P=ab)_(?!(?P=c1)\n)(?!(?P=c2)\n)[0-9a-f]{12}\n", content, re.MULTILINE), content
