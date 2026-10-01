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
import gzip
import shutil
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def test_compress(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """compress.so selects and normalizes supported content encodings.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    __mixed_encodings = "gzip, deflate, sdch, br, zstd"

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create an observing microserver with one object per remap rule.

        :param services: Factory owning support services and their cleanup.
        """

        origin = services.origin(
            "origin",
            options={"--load": str(TEST_DIRECTORY / "compress_observer.py")},
        )
        response = {
            "headers":
                (
                    'HTTP/1.1 200 OK\r\nConnection: close\r\nEtag: "359670651"\r\n'
                    "Cache-Control: public, max-age=31536000\r\nAccept-Ranges: bytes\r\n"
                    "Content-Type: text/javascript\r\n\r\n"),
            "body": _body.decode(),
        }
        for index in range(6):
            origin.add_response(
                {
                    "headers": f"GET /obj{index} HTTP/1.1\r\nHost: just.any.thing\r\n\r\n",
                    "body": ""
                },
                response,
            )
        origin.add_response(
            {
                "headers":
                    (
                        "POST /obj3 HTTP/1.1\r\nHost: just.any.thing\r\n"
                        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n"),
                "body": "knock knock",
            },
            response,
        )
        return origin

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Install six compress configurations with normalization modes 0-5.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_cache=False)
        requirements = {
            "compress.so": ats.plugin_exists("compress.so"),
            "conf_remap.so": ats.plugin_exists("conf_remap.so"),
            "Brotli": ats.has_feature("TS_HAS_BROTLI"),
            "Zstandard": ats.has_feature("TS_HAS_ZSTD"),
        }
        missing = [name for name, available in requirements.items() if not available]
        if missing:
            pytest.skip(f"Required compress features are unavailable: {', '.join(missing)}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "compress",
                "proxy.config.http.normalize_ae": 0,
            })
        configs = [TEST_DIRECTORY / name for name in ("compress.config", "compress2.config", "compress3.config")]
        ats.copy_to_config(*configs)
        for index in range(6):
            config = configs[0 if index < 2 else 1 if index < 4 else 2]
            plugins = ""
            if index:
                plugins += f" @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae={index}"
            plugins += f" @plugin=compress.so @pparam={ats.config_directory / config.name}"
            ats.remap_config.add_line(f"map http://ae-{index}/ http://127.0.0.1:{_origin.port}/{plugins}")
        return ats

    def request(index: int, accept_encoding: str | None, *, post: bool = False) -> tuple[CommandResult, Path]:
        """Issue one curl request and retain its encoded response body.

        :param index: Index used by this test step.
        :param accept_encoding: Accept encoding used by this test step.
        :param post: Post used by this test step.
        """
        nonlocal _request_number

        output = _ats.run_directory / f"response-{_request_number}"
        _request_number += 1
        marker = f"{index}/{accept_encoding}" if accept_encoding is not None else "vary-no-accept-encoding"
        arguments = [
            "--output",
            str(output),
            "--verbose",
            "--proxy",
            f"http://127.0.0.1:{_ats.http_port}",
            "--header",
            f"X-Ats-Compress-Test: {marker}",
        ]
        if accept_encoding is not None:
            arguments.extend(("--header", f"Accept-Encoding: {accept_encoding}"))
        if post:
            arguments.extend(("--data", "knock knock"))
        arguments.append(f"http://ae-{index}/obj{index}")
        result = curl.run_for(
            _ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        return result, output

    def verify_body(path: Path, encoding: str) -> None:
        """Decode @a path according to the expected representation.

        :param path: Resource or file path used by this operation.
        :param encoding: Encoding used by this test step.
        """

        encoded = path.read_bytes()
        if encoding == "identity":
            decoded = encoded
        elif encoding == "gzip":
            decoded = gzip.decompress(encoded)
        else:
            program = {"br": "brotli", "zstd": "zstd"}[encoding]
            if shutil.which(program) is None:
                pytest.skip(f"{program} is required")
            arguments = (program, "-d", "-c", str(path))
            decoded = subprocess.run(arguments, capture_output=True, check=True).stdout
        assert decoded == _body

    def verify_request(index: int, accept_encoding: str, expected_encoding: str) -> None:
        """Require the negotiated representation and its decoded content.

        :param index: Index used by this test step.
        :param accept_encoding: Accept encoding used by this test step.
        :param expected_encoding: Expected encoding for this case.
        """

        result, path = request(index, accept_encoding)
        expected_header = "" if expected_encoding == "identity" else f"< Content-Encoding: {expected_encoding}"
        if expected_header:
            assert expected_header.lower() in result.stderr.lower()
        else:
            assert "< content-encoding:" not in result.stderr.lower()
        verify_body(path, expected_encoding)

    _body = ("lets go surfin now everybodys learnin how\n" * 24 + "lets go surfin now everybodys learnin how").encode()
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)
    _request_number = 0

    _origin.start()
    _ats.start()
    mixed = ("br", "gzip", "br", "br", "zstd", "zstd")
    for index in range(6):
        for value, expected in (
            (__mixed_encodings, mixed[index]),
            ("gzip", "gzip"),
            ("br", "identity" if index == 1 else "br"),
            ("deflate", "identity"),
            ("zstd", "zstd" if index >= 4 else "identity"),
        ):
            verify_request(index, value, expected)

    for value, expected in (
        ("gzip;q=0.666", "gzip"),
        ("gzip;q=0.666x", "gzip"),
        ("gzip;q=#0.666", "gzip"),
        ("gzip; Q = 0.666", "gzip"),
        ("gzip;q=0.0", "identity"),
        ("gzip;q=-0.1", "gzip"),
        ("aaa, gzip;q=0.666, bbb", "gzip"),
        (" br ; q=0.666, bbb", "br"),
        ("aaa, gzip;q=0.666 , ", "gzip"),
    ):
        verify_request(0, value, expected)

    post_result, post_path = request(3, "gzip", post=True)
    assert "< content-encoding: gzip" in post_result.stderr.lower()
    verify_body(post_path, "gzip")

    for accept_encoding in (None, "compress, identity"):
        result, _ = request(0, accept_encoding)
        assert "< vary: accept-encoding" in result.stderr.lower()

    observed = (_origin.run_directory / "compress_userver.log").read_text()
    assert observed == (TEST_DIRECTORY / "compress_userver.gold").read_text()
