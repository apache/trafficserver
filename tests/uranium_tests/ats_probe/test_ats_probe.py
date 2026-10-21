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
import shutil
import subprocess
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
REPLAY_FILE = TEST_DIRECTORY / "ats_probe.replay.yaml"


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the replay's backend hostname to the verifier origin.

    :param services: Factory that owns the DNS process.
    """

    return services.dns("dns", default="127.0.0.1")


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin whose connection should fire the probe.

    :param services: Factory that owns the verifier origin.
    """

    return services.verifier_server("server", REPLAY_FILE, https_ports=[])


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _server: VerifierServer) -> ATS:
    """Route the replay through the test DNS server and origin.

    :param ats_factory: Factory that owns the ATS instance.

    :param _dns: Test-local dns configured by the test.
    :param _server: Test-local server configured by the test.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.remap_config.add_line(f"map / http://backend.server.com:{_server.http_port}")
    return ats


def tracer_command() -> tuple[str, ...]:
    """Run bpftrace directly as root or through passwordless sudo."""

    if shutil.which("bpftrace") is None:
        pytest.skip("bpftrace is required")
    if os.geteuid() == 0:
        return ("bpftrace", str(TEST_DIRECTORY / "ats_probe.bt"))
    if shutil.which("sudo") is not None:
        result = subprocess.run(("sudo", "-n", "true"), capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return ("sudo", "-n", "bpftrace", str(TEST_DIRECTORY / "ats_probe.bt"))
    pytest.skip("ATS probe tracing requires root or passwordless sudo")


def configure_tracer(services: ServiceFactory, *, _tracer_command: tuple[str, ...]) -> ProcessService:
    """Create the bpftrace process for the ATS probe script.

    :param services: Factory that owns the tracer process.

    :param _tracer_command: Test-local tracer command configured by the test.
    """

    return services.process("bpftrace", _tracer_command)


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the request that opens the traced origin connection.

    :param services: Factory that owns the verifier client.

    :param _ats: Test-local ats configured by the test.
    """

    return services.verifier_client("client", REPLAY_FILE, http_ports=[_ats.http_port])


def wait_for_tracer_attach(*, _tracer: ProcessService) -> None:
    """Give bpftrace time to attach and skip unsupported privilege setups.

    :param _tracer: Test-local tracer configured by the test.
    """

    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if not _tracer.is_running:
            pytest.skip(f"bpftrace cannot attach to the ATS probes:\n{_tracer.output}")
        time.sleep(0.1)


def wait_for_probe(*, _tracer: ProcessService) -> None:
    """Require the probe to report the configured backend hostname.

    :param _tracer: Test-local tracer configured by the test.
    """

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if "backend.server.com" in _tracer.output:
            return
        if not _tracer.is_running:
            pytest.skip(f"bpftrace stopped before observing the ATS probe:\n{_tracer.output}")
        time.sleep(0.1)
    raise AssertionError(f"The origin-connection probe did not fire:\n{_tracer.output}")


@pytest.mark.manual(reason="requires privileged bpftrace access")
@pytest.mark.serial
def test_ats_probe(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The origin connection fires its SystemTap-compatible USDT probe.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the scenario's support processes.
    """

    _tracer_command = tracer_command()
    _dns = configure_dns(services)
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _server=_server)
    _tracer = configure_tracer(services, _tracer_command=_tracer_command)
    _client = configure_client(services, _ats=_ats)

    _dns.start()
    _server.start()
    _ats.start()
    _tracer.start()
    wait_for_tracer_attach(_tracer=_tracer)
    result = _client.run()
    assert result.returncode == 0, result.output
    wait_for_probe(_tracer=_tracer)
