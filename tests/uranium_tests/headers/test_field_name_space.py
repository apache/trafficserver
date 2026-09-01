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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class FieldNameSpaceScenario:
    """Forward an origin header containing whitespace before its colon."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the deliberately nonconforming origin response."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nFoo : 123\r\nFoo: 456\r\n",
                "body": "xxx"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Map the synthetic hostname to the malformed origin."""

        ats = ats_factory.create("ts")
        ats.remap_config.add_line(f"map http://www.example.com http://127.0.0.1:{self._origin.port}")
        return ats

    def run(self) -> None:
        """Verify ATS normalizes and preserves both field values."""

        self._origin.start()
        self._ats.start()
        result = self._curl.get(
            self._ats,
            headers={"Host": "www.example.com"},
            options=f"--dump-header - --verbose --http1.1",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stdout, result.output
        assert "Foo: 123" in result.stdout, result.output
        assert "Foo: 456" in result.stdout, result.output
        assert result.stdout.endswith("xxx"), result.output


def test_field_name_space(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS accepts whitespace between an origin field name and colon."""

    FieldNameSpaceScenario(ats_factory, services, curl).run()
