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

import pytest

from tools.uranium.services import ATS, ATSFactory


class ExitOnCertLoadFailScenario:
    """Verify startup policy after server or client certificate load failures."""

    def __init__(self, ats_factory: ATSFactory, side: str, exit_on_failure: bool) -> None:
        self._side = side
        self._exit_on_failure = exit_on_failure
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the selected certificate load failure and exit policy."""

        ats = ats_factory.create("ts", enable_tls=True)
        if self._side == "client":
            ats.add_default_ssl_files()
            ats.ssl_multicert_config.add_lines(
                (
                    "ssl_multicert:",
                    '  - dest_ip: "*"',
                    "    ssl_cert_name: server.pem",
                    "    ssl_key_name: server.key",
                ))
        else:
            ats.set_ssl_multicert_yaml(
                {"ssl_multicert": [{
                    "dest_ip": "*",
                    "ssl_cert_name": "server.pem",
                    "ssl_key_name": "server.key"
                }]})
        client_cert = "NULL" if self._side == "server" else str(ats.ssl_directory / "non-existent-cert.pem")
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.cert.filename": client_cert,
                "proxy.config.ssl.server.multicert.exit_on_load_fail": int(self._exit_on_failure and self._side == "server"),
                "proxy.config.ssl.client.cert.exit_on_load_fail": int(self._exit_on_failure and self._side == "client"),
            })
        ats.remap_config.add_line("map / https://127.0.0.1:12345/")
        if self._exit_on_failure:
            ats.expect_start_failure("EMERGENCY:", return_code=33)
        return ats

    def run(self) -> None:
        """Start ATS and verify its diagnostics and resulting process state."""

        self._ats.start()
        diags = self._ats.diags_log.read_text(errors="replace")
        assert "ERROR:" in diags
        if self._exit_on_failure:
            assert "EMERGENCY:" in diags
            assert "Traffic Server is fully initialized" not in diags
        else:
            assert "Traffic Server is fully initialized" in diags
        if self._side == "server":
            assert re.search(r"ERROR:.*failed to load", diags), diags
        else:
            assert "ERROR: failed to access cert" in diags
            assert "Can't initialize the SSL client, HTTPS in remap rules will not function" in diags


@pytest.mark.parametrize("side", ("server", "client"))
@pytest.mark.parametrize("exit_on_failure", (False, True), ids=("continue", "exit"))
def test_exit_on_cert_load_fail(ats_factory: ATSFactory, side: str, exit_on_failure: bool) -> None:
    """Certificate load failures either log and continue or abort as configured."""

    ExitOnCertLoadFailScenario(ats_factory, side, exit_on_failure).run()
