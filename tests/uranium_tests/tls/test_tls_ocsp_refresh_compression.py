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
"""An OCSP refresh leaves the TLS 1.3 staple in place for clients that accept compressed certificates."""

import subprocess
import time

import pytest
from tools.uranium.services import ATSFactory


def test_tls_ocsp_refresh_keeps_staple_for_compression_clients(ats_factory: ATSFactory) -> None:
    """A TLS 1.3 client that accepts compressed certificates still gets the staple after a refresh pass.

    Certificate compression is not configured, so the certificate must be sent uncompressed, with its staple. The
    certificate names a responder that does not answer, so each pass fails to refresh and the prefetched response stays
    stapled.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    # Without an algorithm both sides support, the certificate is never compressed and the test proves nothing. Only zlib is
    # checked: the client must be OpenSSL 3.2 or later (whose s_client lists -no_rx_cert_comp) built with zlib. BoringSSL
    # never compresses without configured algorithms, so there the test only guards against that changing.
    if not ats_factory.has_feature("TS_HAS_CERT_COMPRESSION_ZLIB"):
        pytest.skip("Traffic Server was built without zlib certificate compression")
    help_text = subprocess.run(["openssl", "s_client", "-help"], capture_output=True, text=True, check=False)
    if "-no_rx_cert_comp" not in help_text.stdout + help_text.stderr:
        pytest.skip("openssl s_client is older than 3.2 and does not offer certificate compression")
    if subprocess.run(["openssl", "zlib"], input=b"x", capture_output=True, check=False).returncode != 0:
        pytest.skip("openssl was built without zlib, so s_client does not offer certificate compression")

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl("ssl/ca.ocsp.pem", "ssl/server.ocsp.pem", "ssl/server.ocsp.key", "ssl/ocsp_response.der")
    ats.ssl_multicert_config.add_lines(
        [
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.ocsp.pem",
            "    ssl_key_name: server.ocsp.key",
            "    ssl_ocsp_name: ocsp_response.der",
        ]
    )
    ats.records.update(
        {
            "proxy.config.ssl.server.cert_chain.filename": "ca.ocsp.pem",
            "proxy.config.ssl.ocsp.response.path": str(ats.ssl_directory),
            "proxy.config.ssl.ocsp.enabled": 1,
            "proxy.config.ssl.ocsp.update_period": 1,
            "proxy.config.ssl.ocsp.cache_timeout": 1,
        }
    )
    ats.allow_errors(r"failed to (get|parse) a response from OCSP server")
    ats.allow_errors(r"OCSP request was timed out")
    ats.allow_errors(r"stapling_refresh_response: failed to refresh OCSP response")
    ats.allow_errors(r"Failed to refresh OCSP for .*server\.ocsp\.pem")
    ats.start()

    # Every pass after the prefetched response's cache timeout tries to refresh it, so the count keeps climbing.
    deadline = time.monotonic() + 30
    failures = 0
    # The counter goes up before a pass finishes with the certificate, so a second failure means a whole pass has run.
    while failures < 2 and time.monotonic() < deadline:
        time.sleep(0.2)
        metric = ats.traffic_ctl("metric", "get", "proxy.process.ssl.ssl_ocsp_refresh_cert_failure")
        assert metric.returncode == 0, metric.output
        failures = int(metric.stdout.split()[-1])
    assert failures >= 2, f"no OCSP refresh pass completed: {failures} failed refreshes"
    result = ats.run_shell(
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} -tls1_3 -servername server.example.com -status -msg </dev/null"
    )
    assert result.returncode == 0, result.output
    assert "CompressedCertificate" not in result.output, f"the certificate was compressed after a refresh: {result.output}"
    assert "OCSP Response Status: successful" in result.output, f"no OCSP response stapled after a refresh: {result.output}"
