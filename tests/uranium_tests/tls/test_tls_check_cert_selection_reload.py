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
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl

TEST_DIRECTORY = Path(__file__).parent


class TlsCertSelectionReloadScenario:
    """Verify that reloading ssl_multicert.yaml replaces the live certificate."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("SNI certificate reload coverage requires a TCP listener")
        self._curl = curl
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install the first bar.com certificate and fallback certificate."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            *(
                TEST_DIRECTORY / "ssl" / name for name in (
                    "signed-bar.pem",
                    "signed2-bar.pem",
                    "signed-bar.key",
                    "signer.pem",
                    "signer.key",
                    "combo.pem",
                )))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                "  - ssl_cert_name: signed-bar.pem",
                "    ssl_key_name: signed-bar.key",
                '  - dest_ip: "*"',
                "    ssl_cert_name: combo.pem",
            ))
        ats.records.update(
            {
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.diags.debug.tags": "ssl|http|lm",
                "proxy.config.diags.debug.enabled": 1,
            })
        return ats

    def request(self, ca_file: str, expected_code: int) -> str:
        """Connect with @a ca_file and verify the expected trust result."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--verbose --cacert '{str(TEST_DIRECTORY / 'ssl' / ca_file)}' --resolve "
                f"'bar.com:{self._ats.https_port}:127.0.0.1' 'https://bar.com:{self._ats.https_port}/random'"),
        )
        assert result.returncode == expected_code, result.output
        return result.output

    def reload_certificate(self) -> None:
        """Replace the live file, advance its mtime, and reload configuration."""

        time.sleep(2)
        live = self._ats.ssl_directory / "signed-bar.pem"
        shutil.copy2(TEST_DIRECTORY / "ssl" / "signed2-bar.pem", live)
        live.touch()
        result = self._ats.traffic_ctl("config", "reload", "-m", "-t", "cert-selection-reload", "-w", "1", "-r", "0.5", "-T", "30s")
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Verify trust switches from signer one to signer two after reload."""

        self._ats.start()
        first = self.request("signer.pem", 0)
        assert "CN=bar.com" in first and "CN=foo.com" not in first
        assert "unable to get local issuer certificate" in self.request("signer2.pem", 60)
        self.reload_certificate()
        assert "unable to get local issuer certificate" in self.request("signer.pem", 60)
        second = self.request("signer2.pem", 0)
        assert "CN=bar.com" in second and "CN=foo.com" not in second


def test_tls_check_cert_selection_reload(ats_factory: ATSFactory, curl: Curl) -> None:
    """Reloading TLS configuration replaces the certificate without restart."""

    TlsCertSelectionReloadScenario(ats_factory, curl).run()
