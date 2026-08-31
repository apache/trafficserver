#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import re

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory


class BodyFactoryContentTypeScenario:
    """Verify that body-factory metadata controls error-response media types."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Configure default and customized body-factory instances.

        :param ats_factory: Factory for isolated ATS processes.
        :param services: Factory used to reserve an unreachable origin port.
        :param curl: Curl command helper.
        """

        self._curl = curl
        unused_port = services.allocate_port()
        self._default = self.configure_ats(ats_factory, "ts-default", unused_port, "Content-Language: en\nContent-Charset: utf-8\n")
        self._custom = self.configure_ats(ats_factory, "ts-custom", unused_port, "Content-Type: text/plain")

    @staticmethod
    def configure_ats(ats_factory: ATSFactory, name: str, origin_port: int, metadata: str) -> ATS:
        """Configure one body-factory instance.

        :param ats_factory: Factory for isolated ATS processes.
        :param name: Unique ATS process name.
        :param origin_port: Unused TCP port in the otherwise-valid remap rule.
        :param metadata: Complete ``.body_factory_info`` contents.
        :return: Configured ATS process.
        """

        ats = ats_factory.create(name)
        ats.records.update({
            "proxy.config.body_factory.enable_customizations": 1,
            "proxy.config.url_remap.remap_required": 1,
        })
        ats.remap_config.add_line(f"map http://mapped.example.com http://127.0.0.1:{origin_port}")
        ats.write_body_factory_file("default/.body_factory_info", metadata)
        return ats

    def response_headers(self, ats: ATS) -> str:
        """Request an unmapped URL and return its response headers.

        :param ats: Configured ATS process to query.
        :return: Curl's response-header output.
        """

        ats.start()
        result = self._curl.get(
            ats,
            "/",
            headers={"Host": "unmapped.example.com"},
            options="--silent --show-error --dump-header - --output /dev/null",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 404" in result.stdout
        return result.stdout

    def run(self) -> None:
        """Verify both the default and explicitly configured media types."""

        default_headers = self.response_headers(self._default)
        assert re.search(r"(?im)^Content-Type:\s*text/html\s*;\s*charset=utf-8\s*$", default_headers)

        custom_headers = self.response_headers(self._custom)
        assert re.search(r"(?im)^Content-Type:\s*text/plain\s*$", custom_headers)


def test_body_factory_content_type(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The body-factory metadata controls the error response Content-Type.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """

    BodyFactoryContentTypeScenario(ats_factory, services, curl).run()
