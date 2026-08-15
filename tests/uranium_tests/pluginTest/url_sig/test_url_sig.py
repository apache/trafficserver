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
"""Verify URL signature validation and exclusion behavior."""

from dataclasses import dataclass
import hashlib
import hmac

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


@dataclass(frozen=True)
class UrlCase:
    """Describe one signed or excluded URL request."""

    name: str
    url: str
    status: str


class UrlSigScenario:
    """Configure curl-specific URL signature tests."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._origin = self.configure_server()
        self._ats = self.configure_ats()
        self._curl = Curl(ats_factory.run_directory)

    def configure_server(self) -> OriginServer:
        """Create origin responses for signed and excluded paths."""

        origin = self._services.origin("origin")
        for path, body in (
            ("/foo/abcde/qrstuvwxyz", ""),
            ("/crossdomain.xml", "crossdomain"),
            ("/clientaccesspolicy.xml", "clientaccess"),
            ("/test.html", "test"),
        ):
            origin.add_response(
                {"headers": f"GET {path} HTTP/1.1\r\nHost: just.any.thing\r\n\r\n"},
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                    "body": body
                },
            )
        return origin

    def configure_ats(self) -> ATS:
        """Install URL-signature mappings for pristine and remapped URLs."""

        ats = self._ats_factory.create("ats", enable_tls=True, enable_cache=False)
        if not ats.plugin_exists("url_sig.so"):
            pytest.skip("url_sig.so is not installed")
        ats.add_default_ssl_files()
        ats.records.update({"proxy.config.proxy_name": "Poxy_Proxy"})
        ats.copy_to_config("url_sig.config", "url_sig.all.config")
        config = ats.config_directory / "url_sig.config"
        all_config = ats.config_directory / "url_sig.all.config"
        target = f"http://127.0.0.1:{self._origin.port}"
        ats.remap_config.add_lines(
            (
                f"map http://one.two.three/ {target}/ @plugin=url_sig.so @pparam={config}",
                f"map https://one.two.three/ {target}/ @plugin=url_sig.so @pparam={config}",
                f"map http://four.five.six/ {target}/ @plugin=url_sig.so @pparam={config} @pparam=pristineurl",
                f"map http://seven.eight.nine/ {target} @plugin=url_sig.so @pparam={config} @pparam=PristineUrl",
                f"map http://ten.eleven.twelve/ {target}/ @plugin=url_sig.so @pparam={all_config}",
            ))
        return ats

    @staticmethod
    def sign(payload: str, key: str) -> str:
        """Return the SHA-1 signature used by the plugin configuration."""

        return hmac.new(key.encode(), payload.encode(), digestmod=hashlib.sha1).hexdigest()

    def request_cases(self) -> tuple[UrlCase, ...]:
        """Build the invalid, excluded, and valid signature matrix."""

        seven = "http://seven.eight.nine/foo/abcde/qrstuvwxyz"
        ten = "http://ten.eleven.twelve"
        invalid = (
            "?C=127.0.0.2&E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=1&A=2&K=13&P=010&S=f237aad1fa010234d7bf8108a0e36387",
            "?C=127.0.0.1&E=33046620008&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=33046620008&A=3&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=33046620008&A=2&K=13&S=d1f352d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=33046620008&A=2&K=13&P=10&S=d1f352d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=33046620008&A=2&K=13&P=101",
            "?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=d1f452d4f1d931ad2f441013402d93f8",
            "?C=127.0.0.1&E=33046620008&A=2&&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8#",
            "?C=127.0.0.1",
            "?E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8&C=127.0.0.1",
            "?C=&E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8",
        )
        cases = [UrlCase(f"invalid-{index}", seven + query, "403 Forbidden") for index, query in enumerate(invalid)]
        cases.extend(
            (
                UrlCase("excluded-crossdomain", f"{ten}/crossdomain.xml", "200 OK"),
                UrlCase("excluded-client-policy", f"{ten}/clientaccesspolicy.xml", "200 OK"),
                UrlCase("excluded-html", f"{ten}/test.html", "200 OK"),
                UrlCase("non-excluded", f"{ten}/other.html", "403 Forbidden"),
                UrlCase(
                    "sha1-client",
                    "http://four.five.six/foo/abcde/qrstuvwxyz"
                    "?C=127.0.0.1&E=33046618556&A=1&K=15&P=1&S=f4103561a23adab7723a89b9831d77e0afb61d92",
                    "200 OK",
                ),
                UrlCase(
                    "md5-no-client",
                    seven + "?E=33046618586&A=2&K=0&P=1&S=0364efa28afe345544596705b92d20ac",
                    "200 OK",
                ),
                UrlCase(
                    "md5-p010",
                    seven + "?C=127.0.0.1&E=33046619717&A=2&K=13&P=010&S=f237aad1fa010234d7bf8108a0e36387",
                    "200 OK",
                ),
                UrlCase(
                    "md5-p101",
                    seven + "?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8",
                    "200 OK",
                ),
            ))
        dynamic_path = "foo/abcde/qrstuvwxyz?E=33046618506&A=1&K=7&P=1&S="
        payload = f"127.0.0.1:{self._origin.port}/{dynamic_path}"
        cases.append(
            UrlCase(
                "non-pristine-sha1",
                f"http://one.two.three/{dynamic_path}{self.sign(payload, 'dqsgopTSM_doT6iAysasQVUKaPykyb6e')}",
                "200 OK",
            ))
        cases.extend(
            (
                UrlCase(
                    "pristine-config",
                    f"{ten}/foo/abcde/qrstuvwxyz"
                    "?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=586ef8e808caeeea025c525c89ff2638",
                    "200 OK",
                ),
                UrlCase(
                    "path-injection",
                    f"{ten}/foo/abcde/qrstuvwxyz;badparam=true"
                    "?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=586ef8e808caeeea025c525c89ff2638",
                    "403 Forbidden",
                ),
                UrlCase(
                    "base64-path-parameter",
                    f"{ten}/foo/abcde;urlsig="
                    "Qz0xMjcuMC4wLjE7RT0zMzA0NjYyMDAwODtBPTI7Sz0xMztQPTEwMTtTPTA1MDllZjljY2VlNjUxZWQ1OTQxM2MyZjE3YmVhODZh"
                    "/qrstuvwxyz",
                    "200 OK",
                ),
            ))
        return tuple(cases)

    def run_http_client(self) -> None:
        """Exercise URL-signature validation over the explicit proxy."""

        proxy = f"http://127.0.0.1:{self._ats.http_port}"
        for case in self.request_cases():
            result = self._curl.run_for(self._ats, "--verbose", "--proxy", proxy, case.url)
            assert result.returncode == 0, f"{case.name}: {result.output}"
            assert f"< HTTP/1.1 {case.status}" in result.stderr, f"{case.name}: {result.output}"

    def run_https_client(self) -> None:
        """Verify a valid non-pristine signature over inbound TLS."""

        path = "foo/abcde/qrstuvwxyz?E=33046618506&A=1&K=7&P=1&S="
        payload = f"127.0.0.1:{self._origin.port}/{path}"
        signature = self.sign(payload, "dqsgopTSM_doT6iAysasQVUKaPykyb6e")
        url = f"https://127.0.0.1:{self._ats.https_port}/{path}{signature}"
        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--http1.1",
            "--insecure",
            "--header",
            "Host: one.two.three",
            url,
        )
        assert result.returncode == 0, result.output
        assert "< HTTP/1.1 200 OK" in result.stderr, result.output

    def run(self) -> None:
        """Start the topology and run both URL-signature transports."""

        self._origin.start()
        self._ats.start()
        self.run_http_client()
        self.run_https_client()
        assert "Error parsing" not in self._ats.diags_log.read_text(errors="replace")


def test_url_sig(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Exercise URL signature checks that depend on curl request syntax."""

    if Curl(ats_factory.run_directory).uses_uds:
        pytest.skip("URL signatures bind client IP addresses")
    UrlSigScenario(ats_factory, services).run()
