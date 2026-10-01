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

GRPC_CLIENT_CONNECTIONS = 50


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the certificate hostname to the local gRPC server.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns", default=["127.0.0.1"])
    return dns


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _server_port: int) -> ATS:
    """Configure TLS ingress and HTTP/2 origin traffic.

    :param _dns: Test-local dns configured by the test.
    :param _server_port: Test-local server port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / https://example.com:{_server_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.http.server_session_sharing.pool": "thread",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http2.min_avg_window_update": 0,
        })
    return ats


def compile_protobuf(*, _generated_directory: Path) -> None:
    """Generate the Python protobuf modules in the scenario sandbox.

    :param _generated_directory: Test-local generated directory configured by the test.
    """

    _generated_directory.mkdir(parents=True)
    command = (
        sys.executable,
        "-m",
        "grpc_tools.protoc",
        f"-I{TEST_DIRECTORY}",
        f"--python_out={_generated_directory}",
        f"--grpc_python_out={_generated_directory}",
        str(TEST_DIRECTORY / "simple.proto"),
    )
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (_generated_directory / "simple_pb2.py").is_file()
    assert (_generated_directory / "simple_pb2_grpc.py").is_file()


def process_environment(*, _generated_directory: Path) -> dict[str, str]:
    """Expose generated modules while preserving the test environment.

    :param _generated_directory: Test-local generated directory configured by the test.
    """

    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(_generated_directory)
    return environment


def configure_server(services: ServiceFactory, *, _generated_directory: Path, _server_port: int) -> ProcessService:
    """Create the finite TLS gRPC origin.

    :param _generated_directory: Test-local generated directory configured by the test.
    :param _server_port: Test-local server port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "server",
        (
            sys.executable,
            TEST_DIRECTORY / "grpc_server.py",
            str(_server_port),
            TOOLS_SSL_DIRECTORY / "server.pem",
            TOOLS_SSL_DIRECTORY / "server.key",
            str(GRPC_CLIENT_CONNECTIONS * 2),
        ),
        environment=process_environment(_generated_directory=_generated_directory),
        ready_port=_server_port,
    )


def configure_client(services: ServiceFactory, *, _ats: ATS, _generated_directory: Path) -> ProcessService:
    """Create the concurrent gRPC client.

    :param _ats: Test-local ats configured by the test.
    :param _generated_directory: Test-local generated directory configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "grpc_client.py",
            "example.com",
            str(_ats.https_port),
            _ats.ssl_directory / "server.pem",
            str(GRPC_CLIENT_CONNECTIONS),
        ),
        environment=process_environment(_generated_directory=_generated_directory),
    )


def test_grpc(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS proxies concurrent TLS gRPC traffic over HTTP/2.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server_port = services.allocate_port()
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _server_port=_server_port)
    _generated_directory = _ats.run_directory / "grpc_generated"
    compile_protobuf(_generated_directory=_generated_directory)
    _server = configure_server(services, _generated_directory=_generated_directory, _server_port=_server_port)
    _client = configure_client(services, _ats=_ats, _generated_directory=_generated_directory)

    _dns.start()
    _server.start()
    _ats.start()
    client_result = _client.run(timeout=30)
    assert "Got the expected 50 responses" in client_result.output
    server_result = _server.wait(timeout=60)
    assert server_result.returncode == 0, server_result.output
