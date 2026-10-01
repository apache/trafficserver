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


def configure_ats(ats_factory: ATSFactory, *, _exit_on_failure: bool, _side: str) -> ATS:
    """Configure the selected certificate load failure and exit policy.

    :param _exit_on_failure: Test-local exit on failure configured by the test.
    :param _side: Test-local side configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    if _side == "client":
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
    client_cert = "NULL" if _side == "server" else str(ats.ssl_directory / "non-existent-cert.pem")
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.cert.filename": client_cert,
            "proxy.config.ssl.server.multicert.exit_on_load_fail": int(_exit_on_failure and _side == "server"),
            "proxy.config.ssl.client.cert.exit_on_load_fail": int(_exit_on_failure and _side == "client"),
        })
    ats.remap_config.add_line("map / https://127.0.0.1:12345/")
    if _exit_on_failure:
        ats.expect_start_failure("EMERGENCY:", return_code=33)
    return ats


@pytest.mark.parametrize("side", ("server", "client"))
@pytest.mark.parametrize("exit_on_failure", (False, True), ids=("continue", "exit"))
def test_exit_on_cert_load_fail(ats_factory: ATSFactory, side: str, exit_on_failure: bool) -> None:
    """Certificate load failures either log and continue or abort as configured.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param side: Side used by this test step.
    :param exit_on_failure: Exit on failure used by this test step.
    """
    _ats = configure_ats(ats_factory, _exit_on_failure=exit_on_failure, _side=side)

    _ats.start()
    diags = _ats.diags_log.read_text(errors="replace")
    assert "ERROR:" in diags
    if exit_on_failure:
        assert "EMERGENCY:" in diags
        assert "Traffic Server is fully initialized" not in diags
    else:
        assert "Traffic Server is fully initialized" in diags
    if side == "server":
        assert re.search(r"ERROR:.*failed to load", diags), diags
    else:
        assert "ERROR: failed to access cert" in diags
        assert "Can't initialize the SSL client, HTTPS in remap rules will not function" in diags
