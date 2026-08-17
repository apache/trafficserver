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


class ConnectionTimeoutScenario:
    """Distinguish a dropped SYN from a connected but delayed origin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Configure the privileged network-namespace scenario.

        :param ats_factory: Factory that owns the ATS instance.
        :param services: Factory that owns the delayed origin process.
        :param curl: Curl client used for timeout requests.
        """

        if curl.uses_uds:
            pytest.skip("the network-namespace scenario requires a TCP listener")
        missing = [program for program in ("ip", "iptables", "nc") if shutil.which(program) is None]
        if missing:
            pytest.skip(f"required network tools are unavailable: {', '.join(missing)}")
        self._privilege = self.privilege_command()
        self._curl = curl
        self._blocked_port = services.allocate_port()
        self._upstream_port = services.allocate_port()
        suffix = str(self._blocked_port)
        self._namespace = f"urtest-{suffix}"
        self._host_interface = f"uh{suffix}"[:15]
        self._namespace_interface = f"un{suffix}"[:15]
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def privilege_command() -> tuple[str, ...]:
        """Return root execution directly or through passwordless sudo."""

        if os.geteuid() == 0:
            return ()
        if shutil.which("sudo") is not None:
            result = subprocess.run(("sudo", "-n", "true"), capture_output=True, text=True, check=False)
            if result.returncode == 0:
                return ("sudo", "-n")
        pytest.skip("network namespace setup requires root or passwordless sudo")

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Create the delayed server inside the test network namespace.

        :param services: Factory that owns the delayed origin process.
        """

        return services.process(
            "delayed-origin",
            (
                *self._privilege,
                "ip",
                "netns",
                "exec",
                self._namespace,
                "nc",
                "-4",
                "-l",
                str(self._upstream_port),
                "-c",
                f"sh {TEST_DIRECTORY / 'delay-server.sh'}",
            ),
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Set a two-second connect timeout and access logging.

        :param ats_factory: Factory that owns the ATS instance.
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
                f"map /blocked http://10.1.1.1:{self._blocked_port}",
                f"map /not-blocked http://10.1.1.1:{self._upstream_port}",
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

    def run_privileged(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        """Run one command with the selected privilege mechanism.

        :param arguments: Command and arguments to execute as root.
        """

        return subprocess.run(
            (*self._privilege, *arguments),
            cwd=TEST_DIRECTORY,
            capture_output=True,
            text=True,
            check=False,
        )

    def setup_namespace(self) -> None:
        """Create the isolated dropped-SYN and delayed-origin network."""

        result = self.run_privileged(
            "sh",
            str(TEST_DIRECTORY / "setupnetns.sh"),
            str(self._blocked_port),
            str(self._upstream_port),
            self._namespace,
            self._host_interface,
            self._namespace_interface,
        )
        if result.returncode != 0:
            self.cleanup_namespace()
            pytest.skip(f"network namespace setup is unavailable:\n{result.stdout}{result.stderr}")

    def cleanup_namespace(self) -> None:
        """Remove only the namespace and interface allocated by this scenario."""

        self.run_privileged("ip", "netns", "del", self._namespace)
        self.run_privileged("ip", "link", "del", self._host_interface)

    def request_blocked(self, method: str) -> None:
        """Require a dropped SYN to reach ATS's connect timeout.

        :param method: HTTP method to send through the blocked connection.
        """

        arguments = ["--include"]
        if method == "POST":
            arguments.extend(("--data", "stuff"))
        arguments.append(f"http://127.0.0.1:{self._ats.http_port}/blocked")
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
            timeout=6,
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 502 internal error - server connection terminated" in result.output

    def request_delayed(self) -> None:
        """Require an established connection to outlive the connect timeout."""

        self._origin.start()
        time.sleep(0.2)
        result = self._curl.run_for(
            self._ats,
            f"--include 'http://127.0.0.1:{self._ats.http_port}/not-blocked'",
            timeout=7,
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200" in result.output
        self._origin.wait(timeout=2)

    def run(self) -> None:
        """Exercise blocked and delayed connections, then clean the namespace."""

        self.setup_namespace()
        try:
            self._ats.start()
            self.request_blocked("GET")
            self.request_blocked("POST")
            self.request_delayed()
            wait_for_file_lines(self._ats.log_directory / "squid.log", r"(?:502|200)", 3)
        finally:
            self.cleanup_namespace()


@pytest.mark.manual(reason="requires privileged network namespace setup")
@pytest.mark.serial
def test_conn_timeout(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS applies connect timeout only while the TCP handshake is pending.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the delayed origin process.
    :param curl: Curl client used for timeout requests.
    """

    ConnectionTimeoutScenario(ats_factory, services, curl).run()
