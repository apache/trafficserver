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

from tools.uranium.services import ATS, ATSFactory, CommandResult, ServiceFactory, VerifierServer


class ChunkExtensionQuotedStringScenario:
    """Reject CR/LF embedded in a chunk-extension quoted string."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._directory = Path(__file__).parent
        self._origin = self.configure_origin()
        self._ats = self.configure_ats()

    def configure_origin(self) -> VerifierServer:
        """Serve the legitimate POST and a sentinel smuggled request."""

        return self._services.verifier_server(
            "verifier-server",
            self._directory / "replays/chunk_extension_quoted_string.replay.yaml",
        )

    def configure_ats(self) -> ATS:
        """Enable strict chunk parsing in Traffic Server."""

        ats = self._ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.strict_chunk_parsing": 1,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}")
        return ats

    def run_client(self, name: str, *, split: bool) -> CommandResult:
        """Run the bespoke client once and validate the anti-smuggling result."""

        command = [
            sys.executable,
            self._directory / "chunk_extension_client.py",
            "127.0.0.1",
            str(self._ats.http_port),
        ]
        if split:
            command.append("--split")
        result = self._services.process(name, command).run()
        assert "responses=1" in result.stdout
        assert "SECOND-ENDPOINT" not in result.stdout
        return result

    def run(self) -> None:
        """Exercise one-write and split-write parser boundaries."""

        self._origin.start()
        self._ats.start()
        self.run_client("quoted-extension", split=False)
        self.run_client("split-quoted-extension", split=True)


def test_chunk_extension_quoted_string(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A malformed chunk extension cannot smuggle a second request."""

    ChunkExtensionQuotedStringScenario(ats_factory, services).run()
