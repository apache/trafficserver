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
import sys

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_file_lines


def test_jax_fingerprint_raw_extensions(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Include ALPS but not GREASE in complete and fragmented Chrome fingerprints.

    :param ats_factory: Factory owning the TLS proxy.
    :param services: Factory owning the raw ClientHello clients.
    """
    ats = ats_factory.create("ts", enable_tls=True)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    ats.add_default_ssl_files()
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "jax_fingerprint"})
    ats.plugin_config.add_line("jax_fingerprint.so --method JA4")
    ats.start()
    # Wireshark's expected fingerprint counts 15 non-GREASE ciphers and 16 extensions, including ALPS.
    fingerprint = "t13d1516h2_8daaf6152771_e5627efa2ab1"
    for size in (0, 100):
        client = services.process(
            f"client-{size}",
            [sys.executable,
             services.resolve_path("chrome_client_hello.py"), "--fragment-size",
             str(size),
             str(ats.https_port)])
        client.stdout.contains(f"Expected JA4: {fingerprint}", "The client must receive a ServerHello with the expected JA4.")
        result = client.run()
        assert result.returncode == 0, result.output
    wait_for_file_lines(ats.traffic_out, f"Fingerprint: {fingerprint}", 2)
    result = ats.traffic_ctl("metric", "get", "proxy.process.ssl.client_hello_raw_parse_failure")
    assert result.returncode == 0, result.output
    assert re.search(r"^proxy\.process\.ssl\.client_hello_raw_parse_failure\s+0$", result.output, re.MULTILINE), result.output
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
