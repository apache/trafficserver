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

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl

TEST_DIRECTORY = Path(__file__).parent
CONTENT = "Some generic content."


class HealthchecksScenario:
    """Verify health-check file watching, full buffers, and concurrent replacement."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self._curl = curl
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("healthchecks.so"):
            pytest.skip("healthchecks.so is required")
        self._acme = self._ats.runtime_directory / "acme"
        self._acme_ssl = self._ats.runtime_directory / "acme-ssl"

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure clear-text and TLS health-check endpoints."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.write_runtime_file("acme", (TEST_DIRECTORY / "acme").read_text())
        ats.write_runtime_file("acme-ssl", (TEST_DIRECTORY / "acme-ssl").read_text())
        ats.write_config_file(
            "healthchecks.config",
            f"/acme {ats.runtime_directory / 'acme'} text/plain 200 404\n"
            f"/acme-ssl {ats.runtime_directory / 'acme-ssl'} text/plain 200 404\n",
        )
        ats.plugin_config.add_line(f"healthchecks.so {ats.config_directory / 'healthchecks.config'}")
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "healthchecks",
            })
        return ats

    def request(self, path: str, *, tls: bool = False, discard: bool = False) -> CommandResult:
        """Request one health-check endpoint."""

        port = self._ats.https_port if tls else self._ats.http_port
        scheme = "https" if tls else "http"
        options = ["--silent", "--insecure", "--write-out", "\n%{http_code}"]
        if discard:
            options.extend(("--output", "/dev/null"))
        options.append(f"{scheme}://127.0.0.1:{port}/{path}")
        return self._curl.run_for(self._ats, *options)

    def wait_for(self, path: str, status: str, *, tls: bool = False, body_length: int | None = None) -> str:
        """Poll until the watched file produces the expected response."""

        deadline = time.monotonic() + 10
        latest = ""
        while time.monotonic() < deadline:
            result = self.request(path, tls=tls)
            assert result.returncode == 0, result.output
            latest = result.stdout
            body, found_status = latest.rsplit("\n", 1)
            if found_status == status and (body_length is None or len(body.encode()) == body_length):
                return body
            time.sleep(0.1)
        raise AssertionError(f"/{path} did not become status {status} with length {body_length}:\n{latest}")

    def rewrite_while_serving(self) -> None:
        """Replace the active file while transactions may still reference its old buffer."""

        with ThreadPoolExecutor(max_workers=8) as executor:
            futures = []
            for iteration in range(10):
                self._acme.write_text(f"{CONTENT} {iteration}\n")
                futures.extend(executor.submit(self.request, "acme", discard=True) for _ in range(2))
            for future in futures:
                result = future.result()
                assert result.returncode == 0, result.output

    def run(self) -> None:
        """Run the health-check file lifecycle."""

        self._ats.start()
        self.wait_for("acme", "200")
        if not self._curl.uses_uds:
            self.wait_for("acme-ssl", "200", tls=True)
            self._acme_ssl.unlink()
        self.wait_for("acme", "200")
        if not self._curl.uses_uds:
            self.wait_for("acme-ssl", "404", tls=True)
            self._acme_ssl.write_text((TEST_DIRECTORY / "acme-ssl").read_text())
            self.wait_for("acme-ssl", "200", tls=True)

        self._acme.write_bytes(b"\0" * 16384)
        self.wait_for("acme", "200", body_length=16384)
        self.rewrite_while_serving()
        final = f"{CONTENT} final\n"
        self._acme.write_text(final)
        assert self.wait_for("acme", "200") == final


def test_healthchecks(ats_factory: ATSFactory, curl: Curl) -> None:
    """The healthchecks plugin follows safe, atomic changes to its content files."""

    HealthchecksScenario(ats_factory, curl).run()
