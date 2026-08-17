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
"""Verify URI signing token extraction and validation."""

from dataclasses import dataclass
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

GOOD_TOKEN = (
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9."
    "eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9."
    "zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY")
EXPIRED_TOKEN = (
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9."
    "eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9."
    "GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8")
SECOND_KEY_TOKEN = (
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9."
    "eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9."
    "ozH4sNwgcOlTZT0l4RQlVCH_osxz9yI1HCBesEv-jYg")
MISSING_ISS_TOKEN = (
    "ewogICJ0eXAiOiAiSldUIiwKICAiYWxnIjogIkhTMjU2Igp9."
    "ewogICJleHAiOiAxOTIzMDU2MDg0Cn0."
    "zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY")


@dataclass(frozen=True)
class RequestCase:
    """Describe one URI-signing request and its expected status."""

    name: str
    url: str
    status: str
    cookie: str | None = None


class UriSigningScenario:
    """Configure and exercise the URI-signing plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._origin = self.configure_server()
        self._ats = self.configure_ats()
        self._curl = Curl(ats_factory.run_directory)

    def configure_server(self) -> OriginServer:
        """Create the three origin resources used by the request matrix."""

        origin = self._services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        origin.add_response(
            {"headers": "GET /someasset.ts HTTP/1.1\r\nHost: somehost\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "somebody"
            },
        )
        origin.add_response(
            {"headers": "GET /crossdomain.xml HTTP/1.1\r\nHost: somehost\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "<crossdomain></crossdomain>",
            },
        )
        return origin

    def configure_ats(self) -> ATS:
        """Configure ATS with URI signing on the somehost mapping."""

        ats = self._ats_factory.create("ats", enable_cache=False)
        if not ats.plugin_exists("uri_signing.so"):
            pytest.skip("uri_signing.so is not installed")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "uri_signing|http",
        })
        ats.copy_to_config("config.json", "run_sign.sh", "signer.json")
        ats.remap_config.add_line(
            f"map http://somehost/ http://127.0.0.1:{self._origin.port}/ "
            f"@plugin=uri_signing.so @pparam={ats.config_directory}/config.json")
        return ats

    @staticmethod
    def request_cases() -> tuple[RequestCase, ...]:
        """Return the URL and cookie token extraction matrix."""

        return (
            RequestCase("unsigned", "/someasset.ts", "403 Forbidden"),
            RequestCase("passthrough", "/crossdomain.xml", "200 OK"),
            RequestCase("query token", f"/someasset.ts?URISigningPackage={GOOD_TOKEN}", "200 OK"),
            RequestCase("expired query token", f"/someasset.ts?URISigningPackage={EXPIRED_TOKEN}", "403 Forbidden"),
            RequestCase("second key", f"/someasset.ts?URISigningPackage={SECOND_KEY_TOKEN}", "200 OK"),
            RequestCase("inline token", f"/URISigningPackage={GOOD_TOKEN}/someasset.ts", "200 OK"),
            RequestCase("expired inline token", f"/URISigningPackage={EXPIRED_TOKEN}/someasset.ts", "403 Forbidden"),
            RequestCase("parameter token", f"/someasset.ts;URISigningPackage={GOOD_TOKEN}", "200 OK"),
            RequestCase(
                "expired parameter token",
                f"/someasset.ts;URISigningPackage={EXPIRED_TOKEN}",
                "403 Forbidden",
            ),
            RequestCase("cookie token", "/someasset.ts", "200 OK", f"URISigningPackage={GOOD_TOKEN}"),
            RequestCase("expired cookie token", "/someasset.ts", "403 Forbidden", f"URISigningPackage={EXPIRED_TOKEN}"),
            RequestCase(
                "multiple cookies",
                "/someasset.ts",
                "200 OK",
                f"URISigningPackage={EXPIRED_TOKEN};URISigningPackage={GOOD_TOKEN}",
            ),
            RequestCase("missing issuer", f"/someasset.ts?URISigningPackage={MISSING_ISS_TOKEN}", "403 Forbidden"),
        )

    def run_client(self) -> None:
        """Issue every curl request and verify the plugin response status."""

        proxy = f"http://127.0.0.1:{self._ats.http_port}"
        for case in self.request_cases():
            arguments = ["--silent", "--show-error", "--verbose", "--proxy", proxy]
            if case.cookie is not None:
                arguments.extend(["--header", f"Cookie: {case.cookie}"])
            arguments.append(f"http://somehost{case.url}")
            result = self._curl.run_for(
                self._ats,
                shlex.join(arguments),
            )
            assert result.returncode == 0, f"{case.name}: {result.output}"
            assert f"< HTTP/1.1 {case.status}" in result.stderr, f"{case.name}: {result.output}"

    def run(self) -> None:
        """Start the topology, exercise it, and validate diagnostics."""

        self._origin.start()
        self._ats.start()
        self.run_client()
        wait_for_file_lines(
            self._ats.traffic_out,
            "Initial JWT Failure: iss is missing, must be present",
            1,
        )


def test_uri_signing(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Exercise URI-signing validation through curl-specific request forms."""

    UriSigningScenario(ats_factory, services).run()
