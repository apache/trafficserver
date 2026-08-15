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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class SslKeyDialogScenario:
    """Load and hot-reload encrypted TLS private keys via a key dialog."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve the success response used before and after reload."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: bogus\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "success!"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install two encrypted keypairs and activate the first one."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "passphrase.pem",
            TEST_DIRECTORY / "ssl" / "passphrase.key",
            TEST_DIRECTORY / "ssl" / "passphrase2.pem",
            TEST_DIRECTORY / "ssl" / "passphrase2.key",
        )
        for hostname in ("passphrase", "passphrase2"):
            ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ http://127.0.0.1:{self._origin.port}")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_load|http",
        })
        ats.ssl_multicert_config.add_lines(self.multicert("passphrase"))
        return ats

    @staticmethod
    def multicert(name: str) -> tuple[str, ...]:
        """Render an encrypted-key ssl_multicert entry."""

        return (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            f"    ssl_cert_name: {name}.pem",
            f"    ssl_key_name: {name}.key",
            '    ssl_key_dialog: "exec:/bin/bash -c \'echo -n passphrase\'"',
        )

    def request(self, hostname: str) -> None:
        """Connect with SNI and validate the encrypted key's certificate."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--cacert",
            str(TEST_DIRECTORY / "ssl" / "signer.pem"),
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            f"https://{hostname}:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        assert "200" in result.stderr
        assert result.stdout == "success!"

    def run(self) -> None:
        """Use the first key, reload to the second, and use it immediately."""

        self._origin.start()
        self._ats.start()
        self.request("passphrase")
        self._ats.ssl_multicert_config.path.write_text("\n".join(self.multicert("passphrase2")) + "\n")
        reload_result = self._ats.traffic_ctl("config", "reload")
        assert reload_result.returncode == 0, reload_result.output
        time.sleep(1)
        self.request("passphrase2")


def test_ssl_key_dialog(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Encrypted TLS keys load and reload through an exec key dialog."""

    SslKeyDialogScenario(ats_factory, services, curl).run()
