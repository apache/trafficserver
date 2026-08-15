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
import sys
import textwrap

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, assert_matches_gold


class BodyFactoryScenario:
    """Drive raw requests that verify body suppression and customization."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._directory = Path(__file__).parent

    def run_client(self, ats: ATS, request_name: str, gold_name: str) -> None:
        """Send a raw request and compare the byte-oriented response."""

        result = self._services.process(
            f"client-{request_name}",
            [
                sys.executable,
                self._directory.parents[1] / "tools/tcp_client.py",
                "127.0.0.1",
                str(ats.http_port),
                self._directory / "data" / request_name,
            ],
        ).run(timeout=10)
        assert_matches_gold(result.stdout, self._directory / "gold" / gold_name)

    def configure_status_ats(self, status: int, host: str, origin: OriginServer) -> ATS:
        """Configure regex_remap to synthesize @a status."""

        ats = self._ats_factory.create("ts")
        ats.write_config_file("maps.reg", f"//.*/ http://127.0.0.1:{origin.port} @status={status}\n")
        ats.remap_config.add_line(
            f"map http://{host} http://127.0.0.1:{origin.port} "
            "@plugin=regex_remap.so @pparam=maps.reg @pparam=no-query-string @pparam=host")
        return ats

    def run_204(self) -> None:
        """Verify standard and explicitly customized 204 responses."""

        default_host = "www.default204.test"
        custom_host = "www.customtemplate204.test"
        origin = self._services.origin("unused-origin")
        ats = self.configure_status_ats(204, default_host, origin)
        ats.records.update({"proxy.config.body_factory.enable_customizations": 3})
        ats.remap_config.add_line(
            f"map http://{custom_host} http://127.0.0.1:{origin.port} "
            "@plugin=regex_remap.so @pparam=maps.reg @pparam=no-query-string @pparam=host "
            f"@plugin=conf_remap.so @pparam=proxy.config.body_factory.template_base={custom_host}")
        ats.write_body_factory_file(
            f"default/{custom_host}_default",
            textwrap.dedent(
                """
                <HTML>
                <HEAD>
                <TITLE>Spec-breaking 204!</TITLE>
                </HEAD>

                <BODY BGCOLOR="white" FGCOLOR="black">
                <H1>This is body content for a 204.</H1>
                <HR>

                <FONT FACE="Helvetica,Arial"><B>
                Description: According to rfc7231 I should not have been sent to you!
                </B></FONT>
                <HR>
                </BODY>
                """).lstrip(),
        )
        ats.start()
        self.run_client(ats, f"{default_host}_get.txt", "http-204.gold")
        self.run_client(ats, f"{custom_host}_get.txt", "http-204-custom.gold")

    def run_plugin_204(self) -> None:
        """Verify a plugin can intentionally attach a body to a 204 response."""

        host = "www.customplugin204.test"
        origin = self._services.origin("unused-origin")
        ats = self.configure_status_ats(204, host, origin)
        ats.write_config_file("maps.reg", "//.*/ http://donotcare.test @status=204\n")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/custom204plugin.so")
        ats.plugin_config.add_line("custom204plugin.so")
        ats.start()
        self.run_client(ats, f"{host}_get.txt", "http-204-custom-plugin.gold")

    def run_304(self) -> None:
        """Verify synthesized 304 responses do not acquire a body."""

        host = "www.default304.test"
        origin = self._services.origin("unused-origin")
        ats = self.configure_status_ats(304, host, origin)
        ats.start()
        self.run_client(ats, f"{host}_get.txt", "http-304.gold")

    def run_head_without_origin(self) -> None:
        """Verify a HEAD failure response does not contain body bytes."""

        ats = self._ats_factory.create("ts")
        ats.start()
        self.run_client(ats, "www.example.test_head.txt", "http-head-no-origin.gold")

    def configure_origin(self) -> OriginServer:
        """Create HEAD, GET, and 304 responses for the origin-backed case."""

        origin = self._services.origin("origin")
        host = "www.example.test"
        origin.add_response(
            {"headers": f"HEAD /head200 HTTP/1.1\r\nHost: {host}\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "This body should not be returned for a HEAD request.",
            },
        )
        origin.add_response(
            {"headers": f"GET /get200 HTTP/1.1\r\nHost: {host}\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "This body should be returned for a GET request.",
            },
        )
        origin.add_response(
            {"headers": f"GET /get304 HTTP/1.1\r\nHost: {host}\r\n\r\n"},
            {"headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def run_with_origin(self) -> None:
        """Verify HEAD body suppression without changing ordinary GET behavior."""

        origin = self.configure_origin()
        ats = self._ats_factory.create("ts")
        ats.remap_config.add_line(f"map http://www.example.test http://127.0.0.1:{origin.port}")
        origin.start()
        ats.start()
        self.run_client(ats, "www.example.test_head_200.txt", "http-head-200.gold")
        self.run_client(ats, "www.example.test_get_200.txt", "http-get-200.gold")
        self.run_client(ats, "www.example.test_get_304.txt", "http-get-304.gold")


def test_http_204_responses(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """204 responses follow RFC body rules unless a customization overrides them."""

    BodyFactoryScenario(ats_factory, services).run_204()


def test_http_204_plugin_response(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A plugin can intentionally create a nonconforming 204 response body."""

    BodyFactoryScenario(ats_factory, services).run_plugin_204()


def test_http_304_response(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """304 responses do not contain bodies."""

    BodyFactoryScenario(ats_factory, services).run_304()


def test_head_without_origin(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HEAD failures return headers without body bytes."""

    BodyFactoryScenario(ats_factory, services).run_head_without_origin()


def test_head_and_get_with_origin(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Origin-backed HEAD and GET responses preserve their body semantics."""

    BodyFactoryScenario(ats_factory, services).run_with_origin()
