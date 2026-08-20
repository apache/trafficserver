#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
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


class H3GoClientScenario:
    """Verify HTTP/3 interoperability with a quic-go client."""

    _replay = "replays/h3_server_for_go_client.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        if not ats_factory.has_feature("TS_USE_QUIC"):
            pytest.skip("ATS was built without QUIC")
        if shutil.which("go") is None:
            pytest.skip("Go 1.24 or newer is required")
        version = re.search(r"go(\d+(?:\.\d+)+)", subprocess.check_output(("go", "version"), text=True))
        if version is None or tuple(int(part) for part in version.group(1).split(".")) < (1, 24):
            pytest.skip("Go 1.24 or newer is required")
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services, ats_factory.run_directory)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the sequential and concurrent request origin."""

        return services.verifier_server("server-go-h3-client", self._replay, verbose=False, other_args="--poll-timeout 30000")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure QUIC limits, routing, and an HTTP/3 access log."""

        ats = ats_factory.create("ts-go-h3-client", enable_tls=True, enable_quic=True, enable_cache=False)
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
                "proxy.config.diags.debug.tags": "quic|http3",
                "proxy.config.quic.initial_max_data_in": 1000000,
                "proxy.config.quic.initial_max_stream_data_bidi_remote_in": 1000000,
                "proxy.config.quic.server.stateless_retry_enabled": 0,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "h3_go_access",
                                    "format": "c_alpn=%<cqssa> client_version=%<cqpv> c_method=%<cqhm> c_url=%<cquuc>",
                                }
                            ],
                        "logs": [{
                            "filename": "h3_go_access",
                            "format": "h3_go_access"
                        }],
                    }
            })
        return ats

    def configure_client(self, services: ServiceFactory, sandbox: Path) -> ProcessService:
        """Create the Go client with isolated build caches."""

        source = shlex.quote(str(TEST_DIRECTORY / "go_h3_client"))
        command = (
            f"cd {source} && exec go run . --addr 127.0.0.1:{self._ats.https_port} "
            f"--authority go.example.com:{self._ats.https_port} --server-name go.example.com")
        environment = {
            **os.environ,
            "GOFLAGS": "-mod=readonly -modcacherw",
            "GOCACHE": str(sandbox / "gocache"),
            "GOMODCACHE": str(sandbox / "gomodcache"),
            "GOTOOLCHAIN": "local",
        }
        return services.process("client", ("/bin/bash", "-c", command), environment=environment)

    def run(self) -> None:
        """Complete all client requests and verify their access-log protocol."""

        self._server.start()
        self._ats.start()
        result = self._client.run(timeout=120)
        assert result.returncode == 0, result.output
        assert "completed 13 HTTP/3 requests" in result.stdout
        get_pattern = r"c_alpn=h3 client_version=http/3 c_method=GET c_url=https://go\.example\.com:[0-9]+/go-get-empty"
        post_pattern = r"c_alpn=h3 client_version=http/3 c_method=POST c_url=https://go\.example\.com:[0-9]+/go-post-large"
        content = wait_for_file_lines(self._ats.log_directory / "h3_go_access.log", post_pattern, 1, timeout=10)
        assert re.search(get_pattern, content)
        assert re.search(post_pattern, content)


def test_h3_go_client(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A quic-go client completes sequential and concurrent requests through ATS."""

    H3GoClientScenario(ats_factory, services).run()
