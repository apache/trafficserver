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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl

OBJECT_BYTES = 102_400


class BigObjectPushScenario:
    """PUSH a large object and retrieve it across client protocols."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        if not Curl.supports("http2"):
            pytest.skip("curl HTTP/2 support is required")
        self._curl = curl
        self._push_file = self.write_push_file(ats_factory.run_directory)
        self._enabled = self.configure_ats(ats_factory, "ts1", push_enabled=True)
        self._disabled = self.configure_ats(ats_factory, "ts2", push_enabled=False)

    def write_push_file(self, run_directory: Path) -> Path:
        """Write the embedded HTTP response consumed by the PUSH method."""

        header = f"HTTP/1.1 200 OK\r\nContent-length: {OBJECT_BYTES}\r\n\r\n"
        path = run_directory / "objfile"
        path.write_text(header + ("x" * OBJECT_BYTES))
        return path

    def configure_ats(self, ats_factory: ATSFactory, name: str, *, push_enabled: bool) -> ATS:
        """Configure one cache with PUSH either enabled or disabled."""

        ats = ats_factory.create(name, enable_tls=True)
        server_ports = (f"{ats.http_port} {ats.ipv6_port}:ipv6 "
                        f"{ats.https_port}:ssl {ats.ipv6_https_port}:ssl:ipv6")
        records: dict[str, object] = {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|cache",
            "proxy.config.http.cache.required_headers": 0,
            "proxy.config.proxy_name": "Poxy_Proxy",
            "proxy.config.url_remap.remap_required": 0,
        }
        if not self._curl.uses_uds:
            records["proxy.config.http.server_ports"] = server_ports
        if push_enabled:
            records["proxy.config.http.push_method_enabled"] = 1
        ats.records.update(records)
        ats.remap_config.add_lines(
            (
                f"map https://localhost:{ats.https_port} http://localhost:{ats.http_port}",
                f"map https://localhost:{ats.ipv6_https_port} http://localhost:{ats.http_port}",
            ))
        return ats

    def push(self, ats: ATS) -> str:
        """PUSH the object into @a ats and return curl's diagnostics."""

        result = self._curl.run_for(
            ats,
            "--verbose",
            "--header",
            "Content-Type: application/octet-stream",
            "--data-binary",
            f"@{self._push_file}",
            "--request",
            "PUSH",
            f"http://localhost:{ats.http_port}/bigobj",
            "--header",
            f"Content-Length: {self._push_file.stat().st_size}",
            timeout=60,
        )
        assert result.returncode == 0, result.output
        return result.output

    def get(self, *arguments: str) -> str:
        """Fetch the pushed object with the requested curl transport options."""

        result = self._curl.run_for(
            self._enabled,
            "--verbose",
            "--output",
            "/dev/null",
            *arguments,
            timeout=60,
        )
        assert result.returncode == 0, result.output
        assert f"content-length: {OBJECT_BYTES}" in result.output.lower()
        return result.output

    def run(self) -> None:
        """Verify PUSH acceptance, retrieval variants, and default rejection."""

        self._enabled.start()
        assert "HTTP/1.1 201 Created" in self.push(self._enabled)
        cleartext_options = () if self._curl.uses_uds else ("--ipv4",)
        assert "HTTP/1.1 200 OK" in self.get(
            *cleartext_options,
            "--http1.1",
            f"http://localhost:{self._enabled.http_port}/bigobj",
        )
        if not self._curl.uses_uds:
            assert "HTTP/1.1 200 OK" in self.get(
                "--ipv4",
                "--http1.1",
                "--insecure",
                f"https://localhost:{self._enabled.https_port}/bigobj",
            )
            assert "HTTP/2 200" in self.get(
                "--ipv4",
                "--http2",
                "--insecure",
                f"https://localhost:{self._enabled.https_port}/bigobj",
            )
            assert "HTTP/2 200" in self.get(
                "--ipv6",
                "--http2",
                "--insecure",
                f"https://localhost:{self._enabled.ipv6_https_port}/bigobj",
            )
        self._disabled.start()
        assert "403 Access Denied" in self.push(self._disabled)


def test_bigobj(ats_factory: ATSFactory, curl: Curl) -> None:
    """Large PUSH objects survive HTTP/1.1, HTTP/2, TLS, and address-family changes."""

    BigObjectPushScenario(ats_factory, curl).run()
