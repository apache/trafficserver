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

from pathlib import Path
import sys

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Install the live and replacement certificates.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "signed-bar.pem",
        TEST_DIRECTORY / "ssl" / "signed-bar.key",
        TEST_DIRECTORY / "ssl" / "signed2-bar.pem",
    )
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: signed-bar.pem",
            "    ssl_key_name: signed-bar.key",
        ))
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl",
        })
    return ats


def test_tls_reload_under_load(ats_factory: ATSFactory) -> None:
    """Certificate reloads remain safe during concurrent TLS handshakes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    result = _ats.run(
        sys.executable,
        TEST_DIRECTORY / "tls_reload_under_load_client.py",
        "-p",
        str(_ats.https_port),
        "--sni",
        "bar.com",
        "--ssldir",
        _ats.ssl_directory,
        "--live-cert",
        "signed-bar.pem",
        "--v2-cert",
        _ats.ssl_directory / "signed2-bar.pem",
        "--reloads",
        "3",
        "--duration",
        "8",
        "--concurrency",
        "4",
        timeout=20,
    )
    assert result.returncode == 0, result.output
    assert "RESULT=PASS" in result.output
    assert "FAILURES=0" in result.output
    assert "CERT_CHANGED=1" in result.output
    wait_for_file_lines(_ats.diags_log, "ssl_multicert.yaml finished loading", 2)
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    for forbidden in ("received signal", "failed assertion", "AddressSanitizer", "use-after-free", "runtime error:"):
        assert forbidden not in traffic_out
