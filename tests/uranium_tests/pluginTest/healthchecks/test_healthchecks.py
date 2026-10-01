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
import shlex
from pathlib import Path
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl

TEST_DIRECTORY = Path(__file__).parent
CONTENT = "Some generic content."


def test_healthchecks(ats_factory: ATSFactory, curl: Curl) -> None:
    """The healthchecks plugin follows safe, atomic changes to its content files.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure clear-text and TLS health-check endpoints.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

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

    def request(path: str, *, tls: bool = False, discard: bool = False) -> CommandResult:
        """Request one health-check endpoint.

        :param path: Resource or file path used by this operation.
        :param tls: Tls used by this test step.
        :param discard: Discard used by this test step.
        """

        port = _ats.https_port if tls else _ats.http_port
        scheme = "https" if tls else "http"
        options = ["--silent", "--insecure", "--write-out", "\n%{http_code}"]
        if discard:
            options.extend(("--output", "/dev/null"))
        options.append(f"{scheme}://127.0.0.1:{port}/{path}")
        return curl.run_for(
            _ats,
            shlex.join(options),
        )

    def wait_for(path: str, status: str, *, tls: bool = False, body_length: int | None = None) -> str:
        """Poll until the watched file produces the expected response.

        :param path: Resource or file path used by this operation.
        :param status: Status used by this test step.
        :param tls: Tls used by this test step.
        :param body_length: Body length used by this test step.
        """

        deadline = time.monotonic() + 10
        latest = ""
        while time.monotonic() < deadline:
            result = request(path, tls=tls)
            assert result.returncode == 0, result.output
            latest = result.stdout
            body, found_status = latest.rsplit("\n", 1)
            if found_status == status and (body_length is None or len(body.encode()) == body_length):
                return body
            time.sleep(0.1)
        raise AssertionError(f"/{path} did not become status {status} with length {body_length}:\n{latest}")

    _ats = configure_ats(ats_factory)
    if not _ats.plugin_exists("healthchecks.so"):
        pytest.skip("healthchecks.so is required")
    _acme = _ats.runtime_directory / "acme"
    _acme_ssl = _ats.runtime_directory / "acme-ssl"

    _ats.start()
    wait_for("acme", "200")
    if not curl.uses_uds:
        wait_for("acme-ssl", "200", tls=True)
        _acme_ssl.unlink()
    wait_for("acme", "200")
    if not curl.uses_uds:
        wait_for("acme-ssl", "404", tls=True)
        _acme_ssl.write_text((TEST_DIRECTORY / "acme-ssl").read_text())
        wait_for("acme-ssl", "200", tls=True)

    _acme.write_bytes(b"\0" * 16384)
    wait_for("acme", "200", body_length=16384)

    with ThreadPoolExecutor(max_workers=8) as executor:
        futures = []
        for iteration in range(10):
            _acme.write_text(f"{CONTENT} {iteration}\n")
            futures.extend(executor.submit(request, "acme", discard=True) for _ in range(2))
        for future in futures:
            result = future.result()
            assert result.returncode == 0, result.output
    final = f"{CONTENT} final\n"
    _acme.write_text(final)
    assert wait_for("acme", "200") == final
