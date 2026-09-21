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
import os
import re
import shlex
import shutil
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class QMuxGoClientScenario:
    """Run the Go HTTP/3-over-QMux interoperability client."""

    _replay = "qmux.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        if not ats_factory.has_feature("TS_USE_QMUX"):
            pytest.skip("ATS was built without QMux")
        if shutil.which("go") is None:
            pytest.skip("Go 1.26 or newer is required")
        version = re.search(r"go(\d+(?:\.\d+)+)", subprocess.check_output(("go", "version"), text=True))
        if version is None or tuple(int(part) for part in version.group(1).split(".")) < (1, 26):
            pytest.skip("Go 1.26 or newer is required")
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services, ats_factory.run_directory)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the QMux transaction origin."""

        return services.verifier_server("server", self._replay, verbose=False)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the h3qx-01 listener and QMux access log."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.set_startup_timeout(60)
        ats.add_default_ssl_files()
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "qmux|http3",
                "proxy.config.http.server_ports": f"{ats.http_port} {ats.https_port}:ssl:proto=h3qx-01",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "qmux_access",
                                    "format": "c_alpn=%<cqssa> client_version=%<cqpv> c_method=%<cqhm> c_url=%<cquuc>",
                                }
                            ],
                        "logs": [{
                            "filename": "qmux_access",
                            "format": "qmux_access"
                        }],
                    }
            })
        return ats

    def configure_client(self, services: ServiceFactory, sandbox: Path) -> ProcessService:
        """Create the Go client with isolated build and module caches."""

        source = shlex.quote(str(TEST_DIRECTORY / "go_qmux_client"))
        command = (
            f"cd {source} && exec go run . --addr 127.0.0.1:{self._ats.https_port} "
            f"--authority qmux.example.com:{self._ats.https_port} --server-name qmux.example.com")
        environment = {
            **os.environ,
            "GOFLAGS": "-mod=readonly -modcacherw",
            "GOCACHE": str(sandbox / "gocache"),
            "GOMODCACHE": str(sandbox / "gomodcache"),
            "GOTOOLCHAIN": "local",
        }
        return services.process("client", ("/bin/bash", "-c", command), environment=environment)

    def run(self) -> None:
        """Complete three requests and verify ATS logged QMux HTTP/3."""

        self._server.start()
        self._ats.start()
        result = self._client.run(timeout=120)
        assert result.returncode == 0, result.output
        assert "completed 3 QMux HTTP/3 requests: alpn=h3qx-01" in result.stdout
        access_log = self._ats.log_directory / "qmux_access.log"
        content = wait_for_file_lines(access_log, r"c_alpn=h3qx-01", 2, timeout=10)
        assert re.search(
            r"c_alpn=h3qx-01 client_version=http/3 c_method=GET "
            r"c_url=https://qmux\.example\.com:[0-9]+/qmux-get-empty",
            content,
        )
        assert re.search(
            r"c_alpn=h3qx-01 client_version=http/3 c_method=POST "
            r"c_url=https://qmux\.example\.com:[0-9]+/qmux-post-large",
            content,
        )


def test_qmux_go_client(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A Go QMux client completes HTTP/3 transactions through ATS."""

    QMuxGoClientScenario(ats_factory, services).run()
