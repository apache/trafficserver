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
"""Verify regex_remap matching and compiled-rule generation sharing."""

import json
import shlex
import os
from pathlib import Path
import time
from typing import Any

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, wait_for_file_lines


class RegexRemapScenario:
    """Exercise regular-expression rules before and after remap reload."""

    ORIGINAL_RULES = (
        "# regex_remap configuration",
        "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ https://redirect.com/ @status=301",
        "^/match_limit/(a+)+$ https://redirect.com/ @status=301",
    )
    UPDATED_RULE = "^/cache-generation$ https://updated.example/ @status=302"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._transactions = self.load_transactions()
        self._origin = self.configure_server()
        self._dns = self.configure_dns()
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    def load_transactions(self) -> list[dict[str, Any]]:
        """Load the three historical request values used by the regression."""

        replay = json.loads(self._services.resolve_path("replay/yts-2819.replay.json").read_text())
        return list(replay["sessions"][0]["transactions"])

    def configure_server(self) -> OriginServer:
        """Create an origin keyed by the transaction UUID header."""

        origin = self._services.origin("origin", lookup_key="{%uuid}")
        for transaction in self._transactions:
            request = transaction["client-request"]
            response = transaction["server-response"]
            uuid = request["headers"]["fields"][1][1]
            response_uuid = response["headers"]["fields"][1][1]
            origin.add_response(
                {"headers": f"GET / HTTP/1.1\r\nHost: example.one\r\nuuid: {uuid}\r\n\r\n"},
                {
                    "headers":
                        (
                            f"HTTP/1.1 {response['status']} {response['reason']}\r\n"
                            f"uuid: {response_uuid}\r\nContent-Length: 6128\r\nConnection: close\r\n\r\n"),
                    "body": "x" * 6128,
                },
            )
        return origin

    def configure_dns(self) -> DNSServer:
        """Resolve fallback remap destinations locally."""

        return self._services.dns("dns", default="127.0.0.1")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure shared and isolated regex rule files."""

        ats = ats_factory.create("ats", enable_cache=False)
        if not ats.plugin_exists("regex_remap.so"):
            pytest.skip("regex_remap.so is not installed")
        ats.write_config_file("regex_remap.conf", "\n".join(self.ORIGINAL_RULES) + "\n")
        ats.write_config_file(
            "regex_remap2.conf",
            (
                "# second regex_remap configuration\n"
                "^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$ "
                f"http://127.0.0.1:{self._origin.port}\n"),
        )
        ats.remap_config.add_lines(
            (
                f"map http://example.one/ http://127.0.0.1:{self._origin.port}/ "
                "@plugin=regex_remap.so @pparam=regex_remap.conf",
                f"map http://example.two/ http://127.0.0.1:{self._origin.port}/ "
                "@plugin=regex_remap.so @pparam=regex_remap.conf @pparam=pristine",
                "map http://example.three/ http://wrong.com/ "
                "@plugin=regex_remap.so @pparam=regex_remap2.conf @pparam=pristine",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|regex_remap",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        return ats

    def request(self, url: str, uuid: str | None = None) -> str:
        """Issue one proxied request and return response headers."""

        arguments = [
            "--silent",
            "--dump-header",
            "-",
            "--output",
            "/dev/null",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
        ]
        if uuid is not None:
            arguments.extend(("--header", f"uuid: {uuid}"))
        arguments.append(url)
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def run_matching_checks(self) -> None:
        """Verify smoke, redirect, pristine remap, and match-limit behavior."""

        smoke, long_match, short_match = self._transactions
        output = self.request(smoke["client-request"]["url"], "smoke")
        assert "HTTP/1.1 200 OK" in output and "uuid: smoke" in output
        path = "/alpha/bravo/?action=newsfed;param0001=00003E;param0002=00004E;param0003=00005E"
        output = self.request(f"http://example.two{path}")
        assert "HTTP/1.1 301 Redirect" in output and "Location: https://redirect.com/" in output
        output = self.request(f"http://example.three{path}", "smoke")
        assert "HTTP/1.1 200 OK" in output and "Content-Length: 6128" in output
        for transaction in (long_match, short_match):
            output = self.request(transaction["client-request"]["url"], transaction["client-request"]["headers"]["fields"][1][1])
            assert "HTTP/1.1 200 OK" in output and "uuid: 180" in output
        wait_for_file_lines(self._ats.diags_log, r"ERROR: .regex_remap. Bad regular expression result -47", 1)

    def reload_rules(self) -> None:
        """Install a new shared generation and reload remap.config."""

        rules_path = self._ats.config_directory / "regex_remap.conf"
        rules_path.write_text("\n".join((self.UPDATED_RULE, *self.ORIGINAL_RULES)) + "\n")
        remap_path = self._ats.config_directory / "remap.config"
        timestamp = max(
            int(time.time()) + 2,
            int(rules_path.stat().st_mtime) + 2,
            int(remap_path.stat().st_mtime) + 2,
        )
        os.utime(rules_path, (timestamp, timestamp))
        os.utime(remap_path, (timestamp, timestamp))
        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        wait_for_file_lines(self._ats.traffic_out, "Reusing cached regular expressions from", 3, timeout=15)

    def run_generation_checks(self) -> None:
        """Verify shared mappings and exact compiled generation counts."""

        for host in ("example.one", "example.two"):
            output = self.request(f"http://{host}/cache-generation")
            assert "HTTP/1.1 302" in output and "Location: https://updated.example/" in output
        output = self.request("http://example.three/cache-generation")
        assert "HTTP/1.1 302" not in output and "Location: https://updated.example/" not in output
        log = self._ats.traffic_out.read_text(errors="replace")
        assert log.count("Cached regular expressions from") == 3
        assert log.count("Reusing cached regular expressions from") == 3
        assert log.count("Compiling regex:") == 6
        cached_lines = [line for line in log.splitlines() if "Cached regular expressions from" in line]
        assert sum("/regex_remap.conf" in line for line in cached_lines) == 2
        assert sum("/regex_remap2.conf" in line for line in cached_lines) == 1
        reused_lines = [line for line in log.splitlines() if "Reusing cached regular expressions from" in line]
        assert sum("/regex_remap.conf" in line for line in reused_lines) == 2
        assert sum("/regex_remap2.conf" in line for line in reused_lines) == 1

    def run(self) -> None:
        """Run matching checks and then verify a reloaded shared generation."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        self.run_matching_checks()
        self.reload_rules()
        self.run_generation_checks()


def test_regex_remap(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """regex_remap shares compiled rules and isolates distinct files."""

    RegexRemapScenario(ats_factory, services).run()
