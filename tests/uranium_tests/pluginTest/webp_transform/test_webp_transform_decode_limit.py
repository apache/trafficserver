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

import struct

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines


def overwide_webp() -> str:
    """Build a minimal ASCII-safe VP8L image declaring a 16129-by-2 canvas."""

    width, height = 16129, 2
    payload = b"\x2f" + struct.pack("<I", (width - 1) | ((height - 1) << 14))
    padded_payload = payload + (b"\x00" if len(payload) % 2 else b"")
    chunk = b"VP8L" + struct.pack("<I", len(payload)) + padded_payload
    body = b"WEBP" + chunk
    body = b"RIFF" + struct.pack("<I", len(body)) + body
    assert all(value <= 0x7f for value in body)
    return body.decode("ascii")


class WebpDecodeLimitScenario:
    """Attempt to decode a tiny image whose declared width exceeds the resource limit."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._body = overwide_webp()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Serve the over-wide VP8L body as image/webp."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /overwide.webp HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nContent-Type: image/webp\r\n"
                        f"Content-Length: {len(self._body)}\r\nConnection: close\r\n\r\n"),
                "body": self._body,
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable the WebP-to-JPEG transform."""

        ats = ats_factory.create("ts", enable_cache=False)
        if not ats.plugin_exists("webp_transform.so"):
            pytest.skip("webp_transform.so is required")
        ats.plugin_config.add_line("webp_transform.so convert_to_jpeg")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "webp_transform",
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    def verify(self, result: CommandResult) -> None:
        """Require the original bytes to pass through after decode rejection."""

        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200" in result.stdout
        assert f"size_download={len(self._body)}" in result.stdout

    def run(self) -> None:
        """Request the image and require the ImageMagick limit diagnostic."""

        self._origin.start()
        self._ats.start()
        result = self._curl.get(
            self._ats,
            "/overwide.webp",
            headers={"Accept": "image/jpeg"},
            options=(
                "--silent",
                "--show-error",
                "--dump-header",
                "-",
                "--output",
                "/dev/null",
                "--write-out",
                "size_download=%{size_download}",
            ),
        )
        self.verify(result)
        wait_for_file_lines(self._ats.diags_log, r"ImageMagick.. error", 1)


def test_webp_transform_decode_limit(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Over-dimension images are rejected before allocating a giant pixel buffer."""

    WebpDecodeLimitScenario(ats_factory, services, curl).run()
