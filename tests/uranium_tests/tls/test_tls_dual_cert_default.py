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
"""Default RSA and EC certificate selection is independent of config order."""

import sys
from pathlib import Path
import pytest
import yaml
from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize("order", ("rsa_ec", "ec_rsa"))
def test_tls_dual_cert_default(ats_factory: ATSFactory, services: ServiceFactory, order: str) -> None:
    """Verify the HTTP exchange and twelve signature-constrained handshakes.

    :param ats_factory: Proxy factory.
    :param services: Verifier and OpenSSL probe factory.
    :param order: Certificate key-type order in the multicert document.
    """
    directory = Path(__file__).parent
    replay = directory / f"replay/dual_cert_default_{order}.replay.yaml"
    origin = services.verifier_server("origin", replay)
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    for name in ("signed-foo", "signed-foo-ec", "signed-san", "signed-san-ec"):
        ats.copy_to_ssl(directory / f"ssl/{name}.pem", directory / f"ssl/{name}.key")
    names = yaml.safe_load(replay.read_text())["tls_multicert"]
    ats.set_ssl_multicert_yaml({"ssl_multicert": names})
    ats.remap_config.add_line(f"map https://example.com/ http://127.0.0.1:{origin.http_port}/")
    origin.start()
    ats.start()
    result = services.verifier_client("client", replay, https_ports=[ats.https_port]).run()
    assert result.returncode == 0, result.output
    result = services.process(
        "certificate-probe", [sys.executable, directory / "tls_dual_cert_default_client.py", str(ats.https_port)]
    ).run()
    assert result.returncode == 0, result.output
    assert "PASS: 12 certificate selections" in result.stdout
