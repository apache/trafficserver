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
import shlex
import os
import shutil
import subprocess
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def privilege_command() -> tuple[str, ...]:
    """Return root execution directly or through passwordless sudo."""

    if os.geteuid() == 0:
        return ()
    if shutil.which("sudo") is not None:
        result = subprocess.run(("sudo", "-n", "true"), capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return ("sudo", "-n")
    pytest.skip("network namespace setup requires root or passwordless sudo")


def configure_origin(
        services: ServiceFactory, *, _namespace: str, _privilege: tuple[str, ...], _upstream_port: int) -> ProcessService:
    """Create the delayed server inside the test network namespace.

    :param services: Factory that owns the delayed origin process.

    :param _namespace: Test-local namespace configured by the test.
    :param _privilege: Test-local privilege configured by the test.
    :param _upstream_port: Test-local upstream port configured by the test.
    """

    return services.process(
        "delayed-origin",
        (
            *_privilege,
            "ip",
            "netns",
            "exec",
            _namespace,
            "nc",
            "-4",
            "-l",
            str(_upstream_port),
            "-c",
            f"sh {TEST_DIRECTORY / 'delay-server.sh'}",
        ),
    )


def configure_ats(ats_factory: ATSFactory, *, _blocked_port: int, _upstream_port: int) -> ATS:
    """Set a two-second connect timeout and access logging.

    :param ats_factory: Factory that owns the ATS instance.

    :param _blocked_port: Test-local blocked port configured by the test.
    :param _upstream_port: Test-local upstream port configured by the test.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.url_remap.remap_required": 1,
            "proxy.config.http.connect_attempts_timeout": 2,
            "proxy.config.http.connect_attempts_max_retries": 0,
            "proxy.config.http.transaction_no_activity_timeout_out": 5,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_lines(
        (
            f"map /blocked http://10.1.1.1:{_blocked_port}",
            f"map /not-blocked http://10.1.1.1:{_upstream_port}",
        ))
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "testformat",
                        "format": "%<pssc> %<pquc> %<pscert> %<cscert>",
                    }],
                    "logs": [{
                        "mode": "ascii",
                        "format": "testformat",
                        "filename": "squid",
                    }],
                }
        })
    return ats


def run_privileged(*arguments: str, _privilege: tuple[str, ...]) -> subprocess.CompletedProcess[str]:
    """Run one command with the selected privilege mechanism.

    :param arguments: Command and arguments to execute as root.

    :param _privilege: Test-local privilege configured by the test.
    """

    return subprocess.run(
        (*_privilege, *arguments),
        cwd=TEST_DIRECTORY,
        capture_output=True,
        text=True,
        check=False,
    )


def setup_namespace(
        *, _blocked_port: int, _host_interface: str, _namespace: str, _namespace_interface: str, _privilege: tuple[str, ...],
        _upstream_port: int) -> None:
    """Create the isolated dropped-SYN and delayed-origin network.

    :param _blocked_port: Test-local blocked port configured by the test.
    :param _host_interface: Test-local host interface configured by the test.
    :param _namespace: Test-local namespace configured by the test.
    :param _namespace_interface: Test-local namespace interface configured by the test.
    :param _privilege: Test-local privilege configured by the test.
    :param _upstream_port: Test-local upstream port configured by the test.
    """

    result = run_privileged(
        "sh",
        str(TEST_DIRECTORY / "setupnetns.sh"),
        str(_blocked_port),
        str(_upstream_port),
        _namespace,
        _host_interface,
        _namespace_interface,
        _privilege=_privilege)
    if result.returncode != 0:
        cleanup_namespace(_host_interface=_host_interface, _namespace=_namespace, _privilege=_privilege)
        pytest.skip(f"network namespace setup is unavailable:\n{result.stdout}{result.stderr}")


def cleanup_namespace(*, _host_interface: str, _namespace: str, _privilege: tuple[str, ...]) -> None:
    """Remove only the namespace and interface allocated by this scenario.

    :param _host_interface: Test-local host interface configured by the test.
    :param _namespace: Test-local namespace configured by the test.
    :param _privilege: Test-local privilege configured by the test.
    """

    run_privileged("ip", "netns", "del", _namespace, _privilege=_privilege)
    run_privileged("ip", "link", "del", _host_interface, _privilege=_privilege)


def request_blocked(method: str, *, _ats: ATS, _curl: Curl) -> None:
    """Require a dropped SYN to reach ATS's connect timeout.

    :param method: HTTP method to send through the blocked connection.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    arguments = ["--include"]
    if method == "POST":
        arguments.extend(("--data", "stuff"))
    arguments.append(f"http://127.0.0.1:{_ats.http_port}/blocked")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=6,
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 502 internal error - server connection terminated" in result.output


def request_delayed(*, _ats: ATS, _curl: Curl, _origin: ProcessService) -> None:
    """Require an established connection to outlive the connect timeout.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    _origin.start()
    time.sleep(0.2)
    result = _curl.run_for(
        _ats,
        f"--include 'http://127.0.0.1:{_ats.http_port}/not-blocked'",
        timeout=7,
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200" in result.output
    _origin.wait(timeout=2)


@pytest.mark.manual(reason="requires privileged network namespace setup")
@pytest.mark.serial
def test_conn_timeout(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS applies connect timeout only while the TCP handshake is pending.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the delayed origin process.
    :param curl: Curl client used for timeout requests.
    """

    if curl.uses_uds:
        pytest.skip("the network-namespace scenario requires a TCP listener")
    missing = [program for program in ("ip", "iptables", "nc") if shutil.which(program) is None]
    if missing:
        pytest.skip(f"required network tools are unavailable: {', '.join(missing)}")
    _privilege = privilege_command()
    _blocked_port = services.allocate_port()
    _upstream_port = services.allocate_port()
    suffix = str(_blocked_port)
    _namespace = f"urtest-{suffix}"
    _host_interface = f"uh{suffix}"[:15]
    _namespace_interface = f"un{suffix}"[:15]
    _origin = configure_origin(services, _namespace=_namespace, _privilege=_privilege, _upstream_port=_upstream_port)
    _ats = configure_ats(ats_factory, _blocked_port=_blocked_port, _upstream_port=_upstream_port)

    setup_namespace(
        _blocked_port=_blocked_port,
        _host_interface=_host_interface,
        _namespace=_namespace,
        _namespace_interface=_namespace_interface,
        _privilege=_privilege,
        _upstream_port=_upstream_port)
    try:
        _ats.start()
        request_blocked("GET", _ats=_ats, _curl=curl)
        request_blocked("POST", _ats=_ats, _curl=curl)
        request_delayed(_ats=_ats, _curl=curl, _origin=_origin)
        wait_for_file_lines(_ats.log_directory / "squid.log", r"(?:502|200)", 3)
    finally:
        cleanup_namespace(_host_interface=_host_interface, _namespace=_namespace, _privilege=_privilege)
