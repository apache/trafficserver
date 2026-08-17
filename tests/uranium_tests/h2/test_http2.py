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
"""Cover HTTP/2 behavior that requires curl or bespoke frame clients."""

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class Http2TlsScenario:
    """Common configuration for a custom HTTP/2 client and HTTP/1 origin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, name: str) -> None:
        self._origin = self.configure_origin(services, name)
        self._ats = self.configure_ats(ats_factory, name)

    @staticmethod
    def configure_origin(services: ServiceFactory, name: str) -> OriginServer:
        """Create a default empty HTTP response origin."""

        origin = services.origin(f"origin-{name}")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str) -> ATS:
        """Configure a TLS HTTP/2 ingress mapped to the origin."""

        ats = ats_factory.create(f"ats-{name}", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http2.active_timeout_in": 3,
                "proxy.config.http2.max_concurrent_streams_in": 65535,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def start(self) -> None:
        """Start the origin and ATS in dependency order."""

        self._origin.start()
        self._ats.start()


class Http2ActiveTimeoutScenario(Http2TlsScenario):
    """Use the frame client that holds an HTTP/2 connection past its timeout."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        super().__init__(ats_factory, services, "active-timeout")

    def run(self) -> None:
        """Verify the connection ends at the configured active timeout."""

        self.start()
        result = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "h2active_timeout.py",
            str(self._ats.https_port),
            "/",
            "4",
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert "CONNECTION_TIMEOUT" in result.output


class Http2ExtensionSettingsScenario(Http2TlsScenario):
    """Send an extension setting before an otherwise ordinary request."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        super().__init__(ats_factory, services, "extension-settings")

    def run(self) -> None:
        """Verify extension settings fit within the default limits."""

        self.start()
        result = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "clients" / "h2_extension_settings.py",
            str(self._ats.https_port),
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert "Received 200 response" in result.stdout


class Http2SettingsRateLimitScenario:
    """Exceed the per-minute setting-change limit with a bespoke client."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Disable per-frame limiting and allow one setting change per minute."""

        ats = ats_factory.create("ats-settings-rate-limit", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.http2.max_settings_per_frame": -1,
                "proxy.config.http2.max_settings_per_minute": 1,
                "proxy.config.http2.max_settings_frames_per_minute": 100,
            })
        return ats

    def run(self) -> None:
        """Verify ATS replies with ENHANCE_YOUR_CALM and logs the reason."""

        self._ats.start()
        result = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "clients" / "h2_max_settings_per_minute.py",
            str(self._ats.https_port),
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert "Received GOAWAY with error code 11" in result.stdout
        wait_for_file_lines(
            self._ats.diags_log,
            r"ERROR: HTTP/2 connection error.*recv settings too frequent setting changes",
            1,
        )


class Http2CurlScenario:
    """Exercise curl's chunked uploads and receipt of very large H2 headers."""

    SMALL_BODY = "1234567890" * 11
    LARGE_BODY = "0123456789" * 131070

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("TLS HTTP/2 curl coverage requires a TCP listener")
        if not curl.supports("http2"):
            pytest.skip("curl does not support HTTP/2")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._large_body_file = ats_factory.run_directory / "big_post_body"
        self._large_body_file.write_text(self.LARGE_BODY)

    @classmethod
    def configure_origin(cls, services: ServiceFactory) -> OriginServer:
        """Create responses for small upload, large upload, and huge headers."""

        origin = services.origin("origin-curl")
        for path, body in (("/postchunked", cls.SMALL_BODY), ("/bigpostchunked", cls.LARGE_BODY)):
            origin.add_response(
                {
                    "headers": f"POST {path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                    "body": body
                },
                {
                    "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nContent-Length: 10\r\n\r\n",
                    "body": "0123456789",
                },
            )
        origin.add_response(
            {"headers": "GET /huge_resp_hdrs HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nContent-Length: 6\r\n\r\n",
                "body": "200 OK",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS and header_rewrite for the large response-header case."""

        ats = ats_factory.create("ats-curl", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        if not ats.plugin_exists("header_rewrite.so"):
            pytest.skip("header_rewrite.so is not installed")
        ats.copy_to_config(TEST_DIRECTORY / "rules" / "huge_resp_hdrs.conf")
        ats.remap_config.add_lines(
            (
                f"map /huge_resp_hdrs http://127.0.0.1:{self._origin.port}/huge_resp_hdrs "
                f"@plugin=header_rewrite.so @pparam={ats.config_directory / 'huge_resp_hdrs.conf'}",
                f"map / http://127.0.0.1:{self._origin.port}",
            ))
        return ats

    def post(self, path: str, data: str) -> None:
        """POST through curl's chunked-input mode and verify the response."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--silent --show-error --insecure --header 'Transfer-Encoding: chunked' --data-binary '{data}' "
                f"'https://127.0.0.1:{self._ats.https_port}{path}'"),
            timeout=30,
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "0123456789", result.output

    def verify_huge_response_headers(self) -> None:
        """Verify six large fields survive HTTP/2 header encoding and decoding."""

        result = self._curl.run_for(
            self._ats,
            f"--verbose --silent --insecure --http2 'https://127.0.0.1:{self._ats.https_port}/huge_resp_hdrs'",
            timeout=30,
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "200 OK", result.output
        assert "HTTP/2 200" in result.stderr, result.output
        for index in range(6):
            assert f"x-huge-{index}:" in result.stderr.lower(), result.output

    def run(self) -> None:
        """Run both chunked uploads and the huge response-header request."""

        self._origin.start()
        self._ats.start()
        self.post("/postchunked", self.SMALL_BODY)
        self.post("/bigpostchunked", f"@{self._large_body_file}")
        self.verify_huge_response_headers()


def test_http2_active_timeout(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The custom client observes the configured H2 active timeout."""

    Http2ActiveTimeoutScenario(ats_factory, services).run()


def test_http2_extension_settings(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """An extension setting does not prevent the following request."""

    Http2ExtensionSettingsScenario(ats_factory, services).run()


def test_http2_settings_rate_limit(ats_factory: ATSFactory) -> None:
    """Frequent setting changes trigger ENHANCE_YOUR_CALM."""

    Http2SettingsRateLimitScenario(ats_factory).run()


def test_http2_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Curl can upload chunked bodies and receive huge HTTP/2 fields."""

    Http2CurlScenario(ats_factory, services, curl).run()
