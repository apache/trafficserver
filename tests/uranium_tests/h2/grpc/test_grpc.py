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
import subprocess
import sys

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TOOLS_SSL_DIRECTORY = TEST_DIRECTORY.parents[2] / "tools" / "ssl"


class GrpcScenario:
    """Proxy concurrent TLS gRPC requests over HTTP/2."""

    CLIENT_CONNECTIONS = 50

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server_port = services.allocate_port()
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)
        self._generated_directory = self._ats.run_directory / "grpc_generated"
        self.compile_protobuf()
        self._server = self.configure_server(services)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the certificate hostname to the local gRPC server."""

        dns = services.dns("dns", default=["127.0.0.1"])
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS ingress and HTTP/2 origin traffic."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / https://example.com:{self._server_port}/")
        ats.records.update(
            {
                "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
                "proxy.config.http.server_session_sharing.pool": "thread",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http2.min_avg_window_update": 0,
            })
        return ats

    def compile_protobuf(self) -> None:
        """Generate the Python protobuf modules in the scenario sandbox."""

        self._generated_directory.mkdir(parents=True)
        command = (
            sys.executable,
            "-m",
            "grpc_tools.protoc",
            f"-I{TEST_DIRECTORY}",
            f"--python_out={self._generated_directory}",
            f"--grpc_python_out={self._generated_directory}",
            str(TEST_DIRECTORY / "simple.proto"),
        )
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        assert (self._generated_directory / "simple_pb2.py").is_file()
        assert (self._generated_directory / "simple_pb2_grpc.py").is_file()

    def process_environment(self) -> dict[str, str]:
        """Expose generated modules while preserving the test environment."""

        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(self._generated_directory)
        return environment

    def configure_server(self, services: ServiceFactory) -> ProcessService:
        """Create the finite TLS gRPC origin."""

        return services.process(
            "server",
            (
                sys.executable,
                TEST_DIRECTORY / "grpc_server.py",
                str(self._server_port),
                TOOLS_SSL_DIRECTORY / "server.pem",
                TOOLS_SSL_DIRECTORY / "server.key",
                str(self.CLIENT_CONNECTIONS * 2),
            ),
            environment=self.process_environment(),
            ready_port=self._server_port,
        )

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the concurrent gRPC client."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "grpc_client.py",
                "example.com",
                str(self._ats.https_port),
                self._ats.ssl_directory / "server.pem",
                str(self.CLIENT_CONNECTIONS),
            ),
            environment=self.process_environment(),
        )

    def run(self) -> None:
        """Run the DNS, origin, ATS, and client topology."""

        self._dns.start()
        self._server.start()
        self._ats.start()
        client_result = self._client.run(timeout=30)
        assert "Got the expected 50 responses" in client_result.output
        server_result = self._server.wait(timeout=60)
        assert server_result.returncode == 0, server_result.output


def test_grpc(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS proxies concurrent TLS gRPC traffic over HTTP/2."""

    GrpcScenario(ats_factory, services).run()
