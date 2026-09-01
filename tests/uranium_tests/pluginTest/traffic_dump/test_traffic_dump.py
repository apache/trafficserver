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
"""Verify traffic_dump serialization and runtime disk-limit controls."""

from pathlib import Path
import re
import sys
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"
CLIENT_REPLAY = TEST_DIRECTORY / "replay" / "traffic_dump.yaml"
SERVER_REPLAY = TEST_DIRECTORY / "replay" / "traffic_dump_server.yaml"


class TrafficDumpScenario:
    """Capture a protocol matrix, validate dumps, and change limits live."""

    SENSITIVE_FIELDS = ("cookie", "set-cookie", "x-request-1", "x-request-2")

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._dump_directory = self._ats.log_directory / "127"

    @staticmethod
    def configure_server(services: ServiceFactory) -> VerifierServer:
        """Create the HTTP and TLS origin used by the replay matrix."""

        return services.verifier_server(
            "server",
            SERVER_REPLAY,
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS, remaps, logging, and sensitive-field redaction."""

        ats = ats_factory.create("ats", enable_tls=True)
        if not ats.plugin_exists("traffic_dump.so"):
            pytest.skip("traffic_dump.so is not installed")
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "server.pem",
            TEST_DIRECTORY / "ssl" / "server.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.set_ssl_multicert_yaml(
            {"ssl_multicert": [{
                "dest_ip": "*",
                "ssl_cert_name": "server.pem",
                "ssl_key_name": "server.key"
            },]})
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "traffic_dump|http",
                "proxy.config.http.insert_age_in_response": 0,
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.host_sni_policy": 2,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.http.connect_ports": str(self._server.http_port),
            })
        ats.remap_config.add_lines(
            (
                f"map https://www.client_only_tls.com/ http://127.0.0.1:{self._server.http_port}",
                f"map https://www.tls.com/ https://127.0.0.1:{self._server.https_port}",
                f"map http://www.connect_target.com/ http://127.0.0.1:{self._server.http_port}",
                f"map / http://127.0.0.1:{self._server.http_port}",
            ))
        ats.allow_private_connect(("CONNECT", "GET", "POST"))
        ats.plugin_config.add_line(
            f'traffic_dump.so --logdir {ats.log_directory} --sample 1 --limit 1000000000 '
            '--sensitive-fields "cookie,set-cookie,x-request-1,x-request-2"')
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [{
                                "name": "basic",
                                "format": "%<cluc>: Read result: %<crc>:%<crsc>:%<chm>, Write result: %<cwr>",
                            }],
                        "logs": [{
                            "filename": "transactions",
                            "format": "basic"
                        }],
                    }
            })
        return ats

    def configure_client(self, name: str, *, keys: str | None = None) -> ProcessService:
        """Create a verifier client for the full matrix or one selected session."""

        return self._services.verifier_client(
            name,
            CLIENT_REPLAY,
            http_ports=[self._ats.http_port],
            https_ports=[self._ats.https_port],
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
            keys=keys,
        )

    def wait_for_dump(self, index: int) -> Path:
        """Wait for one numbered dump file to be written."""

        path = self._dump_directory / f"{index:016x}"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if path.is_file() and path.stat().st_size:
                return path
            time.sleep(0.1)
        raise AssertionError(f"traffic_dump did not write {path}")

    def verify_dump(self, index: int, *arguments: str, sensitive: bool = False) -> None:
        """Run the replay schema and semantic validator on one dump."""

        command: list[str | Path] = [
            sys.executable,
            TEST_DIRECTORY / "verify_replay.py",
            TEST_TOOLS / "lib" / "replay_schema.json",
            self.wait_for_dump(index),
        ]
        if sensitive:
            for field in self.SENSITIVE_FIELDS:
                command.extend(("--sensitive-fields", field))
        command.extend(arguments)
        result = self._ats.run(*command)
        assert result.returncode == 0, result.output

    def verify_initial_dumps(self) -> None:
        """Validate targets, bodies, cache traffic, protocols, TLS, and CONNECT."""

        client_tls = "sni:www.tls.com,proxy-verify-mode:0,proxy-provided-cert:true"
        server_tls = "proxy-provided-cert:false,sni:www.tls.com,proxy-verify-mode:1"
        self.verify_dump(0, "--client-http-version", "1.1", "--client-protocols", "tcp,ip", sensitive=True)
        self.verify_dump(1, "--client-http-version", "1.1", "--request-target", "/two", sensitive=True)
        self.verify_dump(2, "--request-target", "http://www.some.host.com/candy", sensitive=True)
        self.verify_dump(3, "--client-request-size", "12345", sensitive=True)
        self.verify_dump(5, "--client-protocols", "tcp,ip")
        self.verify_dump(6, "--client-protocols", "tcp,ip")
        self.verify_dump(7, "--client-protocols", "tls,tcp,ip", "--client-tls-features", client_tls)
        self.verify_dump(7, "--server-protocols", "http,tls,tcp,ip", "--server-tls-features", server_tls)
        self.verify_dump(
            8,
            "--client-http-version",
            "2",
            "--client-protocols",
            "http,tls,tcp,ip",
            "--client-tls-features",
            client_tls,
        )
        self.verify_dump(8, "--server-protocols", "http,tls,tcp,ip", "--server-tls-features", server_tls)
        self.verify_dump(9, "--client-http-version", "1.1", "--client-protocols", "tls,tcp,ip")
        self.verify_dump(9, "--server-protocols", "http,tcp,ip")
        self.verify_dump(10, sensitive=True)

    def set_limit(self, value: int | None) -> None:
        """Set a byte limit or remove the limit and wait for plugin delivery."""

        arguments = ("plugin", "msg", "traffic_dump.unlimit") if value is None else (
            "plugin",
            "msg",
            "traffic_dump.limit",
            str(value),
        )
        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        time.sleep(2)

    def run_limit_cases(self) -> None:
        """Verify limit, unlimit, and re-limit behavior on new sessions."""

        self.set_limit(0)
        assert self.configure_client("client-limited", keys="1").run().returncode == 0
        time.sleep(2)
        assert not (self._dump_directory / f"{11:016x}").exists()

        self.set_limit(None)
        assert self.configure_client("client-unlimited", keys="1").run().returncode == 0
        self.wait_for_dump(12)

        self.set_limit(0)
        assert self.configure_client("client-relimited", keys="1").run().returncode == 0
        time.sleep(2)
        assert not (self._dump_directory / f"{13:016x}").exists()

    def assert_plugin_diagnostics(self) -> None:
        """Verify traffic_dump loaded, initialized, and completed sessions."""

        diags = self._ats.diags_log.read_text(errors="replace")
        assert re.search(r"loading plugin.*traffic_dump\.so", diags)
        traffic = wait_for_file_lines(self._ats.traffic_out, "Finish a session with log file of.*bytes", 1)
        assert f"Initialized with log directory: {self._ats.log_directory}" in traffic
        assert "Initialized with sample pool size of 1 bytes and disk limit of 1000000000 bytes" in traffic
        assert "Dumping body bytes: false" in traffic

    def run(self) -> None:
        """Capture the initial matrix, validate it, and exercise live limits."""

        self._server.start()
        self._ats.start()
        initial = self.configure_client("client").run()
        assert initial.returncode == 0, initial.output
        self.verify_initial_dumps()
        self.run_limit_cases()
        self.assert_plugin_diagnostics()


def test_traffic_dump(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump emits valid redacted replays and honors live disk limits."""

    TrafficDumpScenario(ats_factory, services).run()
