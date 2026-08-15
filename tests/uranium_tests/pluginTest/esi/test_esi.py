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
"""Verify ESI transformation, gzip, cache-control, and size options."""

from dataclasses import dataclass
import gzip
from pathlib import Path
import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines

ESI_BODY = """<?php   header('X-Esi: 1'); ?>
<html>
<body>
Hello, <esi:include src="http://www.example.com/date.php"/>
</body>
</html>
"""
DATE_BODY = """<?php
header ("Cache-control: no-cache");
echo date('l jS \\of F Y h:i:s A');
?>
"""
TRANSFORMED_BODY = ESI_BODY.replace('<esi:include src="http://www.example.com/date.php"/>', DATE_BODY)


@dataclass(frozen=True)
class EsiVariant:
    """Describe one ESI plugin option combination."""

    name: str
    plugin_config: str
    behavior: str
    private_response: bool = False


VARIANTS = (
    EsiVariant("vanilla", "esi.so", "gzip"),
    EsiVariant("private-response", "esi.so --private-response", "gzip", private_response=True),
    EsiVariant("first-byte-flush", "esi.so --first-byte-flush", "gzip"),
    EsiVariant("disable-gzip", "esi.so --disable-gzip-output", "no-gzip"),
    EsiVariant("max-doc-100", "esi.so --max-doc-size 100", "too-small"),
    EsiVariant("max-doc-2k", "esi.so --max-doc-size 2K", "gzip"),
    EsiVariant("max-doc-20m", "esi.so --max-doc-size 20M", "gzip"),
    EsiVariant("allowed-200", "esi.so --allowed-response-codes 200", "gzip"),
    EsiVariant("allowed-304", "esi.so --allowed-response-codes 304", "no-transform"),
)


class EsiScenario:
    """Configure one ESI option variant and run its client checks."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, variant: EsiVariant) -> None:
        self._variant = variant
        self._run_directory = ats_factory.run_directory
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @staticmethod
    def configure_server(services: ServiceFactory) -> OriginServer:
        """Create the document, include fragment, and empty response."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET /esi.php HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nX-Esi: 1\r\nConnection: close\r\n"
                        f"Content-Length: {len(ESI_BODY)}\r\nCache-Control: max-age=300\r\n\r\n"),
                "body": ESI_BODY,
            },
        )
        origin.add_response(
            {"headers": "GET /date.php HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n"
                        f"Content-Length: {len(DATE_BODY)}\r\nCache-Control: max-age=300\r\n\r\n"),
                "body": DATE_BODY,
            },
        )
        origin.add_response(
            {"headers": "GET /expect_empty_body HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nX-ESI: On\r\nContent-Length: 0\r\nConnection: close\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n\r\n")
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the selected ESI configuration globally."""

        ats = ats_factory.create("ats")
        if not ats.plugin_exists("esi.so"):
            pytest.skip("esi.so is not installed")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|plugin_esi",
        })
        ats.remap_config.add_line(f"map http://www.example.com/ http://127.0.0.1:{self._origin.port}")
        ats.plugin_config.add_line(self._variant.plugin_config)
        return ats

    def request(self, path: str, *options: str) -> CommandResult:
        """Issue a verbose ESI request with the required host headers."""

        return self._curl.get(
            self._ats,
            path,
            headers={
                "Host": "www.example.com",
                "Accept": "*/*"
            },
            options=("--verbose", *options),
        )

    def assert_transformed(self, result: CommandResult) -> None:
        """Verify transformed content and cache-control behavior."""

        assert result.returncode == 0, result.output
        assert "< HTTP/1.1 200 OK" in result.stderr, result.output
        assert "< Content-Type: text/html" in result.stderr, result.output
        assert TRANSFORMED_BODY in result.stdout, result.output
        headers = result.stderr.lower()
        if self._variant.private_response:
            assert re.search(r"cache-control:.*max-age=0, private", headers), result.output
            assert "expires: -1" in headers, result.output
        else:
            assert "cache-control:" not in headers, result.output
            assert "expires:" not in headers, result.output

    def assert_gzip(self, path: str, output_name: str, *, empty: bool = False) -> None:
        """Download and decompress a gzip response."""

        output_path = self._run_directory / output_name
        result = self.request(path, "--header", "Accept-Encoding: gzip", "--output", str(output_path))
        assert result.returncode == 0, result.output
        assert "< Content-Encoding: gzip" in result.stderr, result.output
        decompressed = gzip.decompress(output_path.read_bytes())
        assert decompressed == (b"" if empty else TRANSFORMED_BODY.encode())

    def run_gzip_cases(self) -> None:
        """Verify uncached, cached, compressed, and compressed-empty output."""

        self.assert_transformed(self.request("/esi.php"))
        self.assert_transformed(self.request("/esi.php"))
        self.assert_gzip("/esi.php", "esi-body.gz")
        self.assert_gzip("/expect_empty_body", "empty.gz", empty=True)

    def run_no_gzip_cases(self) -> None:
        """Verify gzip output remains disabled when the client accepts it."""

        self.assert_transformed(self.request("/esi.php"))
        result = self.request("/esi.php", "--header", "Accept-Encoding: gzip")
        self.assert_transformed(result)
        assert "Content-Encoding: gzip" not in result.stderr

    def run(self) -> None:
        """Dispatch the checks appropriate for this plugin configuration."""

        self._origin.start()
        self._ats.start()
        if self._variant.behavior == "gzip":
            self.run_gzip_cases()
        elif self._variant.behavior == "no-gzip":
            self.run_no_gzip_cases()
        elif self._variant.behavior == "too-small":
            result = self.request("/esi.php")
            assert result.returncode == 0, result.output
            wait_for_file_lines(
                self._ats.diags_log,
                r"ERROR: \[_setup\] Cannot allow attempted doc of size 121; Max allowed size is 100 for URL \[.*esi\.php.*\]",
                1,
            )
        else:
            result = self.request("/esi.php")
            assert result.returncode == 0, result.output
            assert 'Hello, <esi:include src="http://www.example.com/date.php"/>' in result.stdout


@pytest.mark.parametrize("variant", VARIANTS, ids=lambda value: value.name)
def test_esi(ats_factory: ATSFactory, services: ServiceFactory, variant: EsiVariant) -> None:
    """ESI options preserve their documented transformation behavior."""

    EsiScenario(ats_factory, services, variant).run()
