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
"""Verify slice behavior when an origin replaces an object in place."""

from pathlib import Path
import time

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    DNSServer,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class SliceHierarchyScenario:
    """Build a slicing child, a parent cache, and a replay origin."""

    BLOCK_BYTES = 16
    CLIENT_REPLAY = TEST_DIRECTORY / "replay" / "slice_stale_generation_client.replay.yaml"
    SERVER_REPLAY = TEST_DIRECTORY / "replay" / "slice_stale_generation_server.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, name: str) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._name = name
        self._dns = self.configure_dns()
        self._origin = self.configure_origin()
        self._parent = self.configure_parent()
        self._child = self.configure_child("child")

    def configure_dns(self) -> DNSServer:
        """Resolve the logical origin name to loopback."""

        return self._services.dns(f"dns-{self._name}", default="127.0.0.1")

    def configure_origin(self) -> VerifierServer:
        """Key replay responses by URL, byte range, and phase UUID."""

        return self._services.verifier_server(
            f"origin-{self._name}",
            self.SERVER_REPLAY,
            other_args='--format "{url}{field.range}{field.uuid}"',
        )

    def slice_remap(self, source: str, upstream: str) -> str:
        """Build a remap rule with slice before cache_range_requests."""

        return (
            f"map {source} {upstream} @plugin=slice.so @pparam=--blockbytes-test={self.BLOCK_BYTES} "
            "@plugin=cache_range_requests.so")

    def configure_records(self, ats: ATS) -> None:
        """Apply records shared by both cache tiers."""

        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "slice|cache_range_requests",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.parent_proxy.self_detect": 0,
            })

    def require_plugins(self, ats: ATS) -> None:
        """Skip unless the hierarchy plugins and diagnostic plugin are installed."""

        required = ("slice.so", "cache_range_requests.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")

    def configure_parent(self) -> ATS:
        """Configure the cache that stores independent per-Range objects."""

        parent = self._ats_factory.create(f"parent-{self._name}")
        self.require_plugins(parent)
        parent.remap_config.add_line(self.slice_remap("http://origin.test/", f"http://127.0.0.1:{self._origin.http_port}/"))
        parent.plugin_config.add_line("xdebug.so --enable=x-cache")
        self.configure_records(parent)
        return parent

    def configure_child(self, label: str) -> ATS:
        """Configure a child that slices requests and forwards blocks to the parent."""

        child = self._ats_factory.create(f"{label}-{self._name}")
        child.remap_config.add_line(self.slice_remap("http://slice/", "http://origin.test/"))
        child.parent_config.add_line(
            f"dest_domain=. parent=127.0.0.1:{self._parent.http_port} round_robin=consistent_hash go_direct=false")
        child.plugin_config.add_line("xdebug.so --enable=x-cache")
        self.configure_records(child)
        return child

    def configure_client(
        self,
        phase: str,
        *,
        ats: ATS | None = None,
        expected_return_code: int = 0,
    ) -> ProcessService:
        """Create a phase-selected replay client for one child."""

        target = self._child if ats is None else ats
        return self._services.verifier_client(
            f"client-{phase}-{self._name}",
            self.CLIENT_REPLAY,
            http_ports=[target.http_port],
            keys=phase,
            return_code=expected_return_code,
            allow_errors=expected_return_code != 0,
        )

    def start_hierarchy(self) -> None:
        """Start DNS, origin, parent, and the primary child in dependency order."""

        self._dns.start()
        self._origin.start()
        self._parent.start()
        self._child.start()

    def assert_parent_bypass(self) -> None:
        """Verify child block requests bypass slice on the parent."""

        output = self._parent.traffic_out.read_text(errors="replace")
        assert "slice passing GET or HEAD request through to next plugin" in output
        assert "slice accepting and slicing" not in output


class SliceStaleGenerationScenario(SliceHierarchyScenario):
    """Verify a cached reference block pins a uniformly stale identity."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        super().__init__(ats_factory, services, "stale")

    def run(self) -> None:
        """Fill the old object, then test clipped, refused, and uncached ranges."""

        self.start_hierarchy()
        for phase in ("fill", "clipped", "unsatisfiable", "control"):
            result = self.configure_client(phase).run()
            assert result.returncode == 0, result.output

        origin_output = self._origin.output
        for key in ("/objbytes=0-15clipped", "/objbytes=0-15unsatisfiable"):
            assert f"request with key {key}" not in origin_output
        child_diags = self._child.diags_log.read_text(errors="replace")
        assert "logSliceError" not in child_diags
        assert "Mismatch/Bad block Content-Range" not in child_diags
        self.assert_parent_bypass()


class SliceMixedGenerationScenario(SliceHierarchyScenario):
    """Verify an independent parent range cache can hold an unrecoverable mix."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        super().__init__(ats_factory, services, "mixed")
        self._cold_child = self.configure_child("cold-child")

    def assert_mismatch_diagnostics(self, ats: ATS, *, both_blocks: bool) -> None:
        """Verify the child reports the expected Content-Range identity mismatch."""

        content = wait_for_file_lines(ats.diags_log, "Mismatch/Bad block Content-Range", 1)
        assert 'blk_range="16-31"' in content
        assert 'etag_got="%22v1%22"' in content
        if both_blocks:
            content = wait_for_file_lines(ats.diags_log, "Mismatch/Bad block Content-Range", 2)
            assert 'blk_range="0-15"' in content
            assert 'etag_got="%22v2%22"' in content

    def run(self) -> None:
        """Create a parent-side mix and prove a cold child inherits it."""

        self.start_hierarchy()
        fill = self.configure_client("fill-interior").run()
        assert fill.returncode == 0, fill.output

        time.sleep(2)
        mixed = self.configure_client("mixed", expected_return_code=1).run()
        assert "Failed to find a well-formed, completed HTTP response: PARSE_INCOMPLETE" in mixed.output
        assert "Failed HTTP/1 transaction with key: mixed" in mixed.output
        self.assert_mismatch_diagnostics(self._child, both_blocks=True)

        self._cold_child.start()
        cold = self.configure_client("cold-child", ats=self._cold_child, expected_return_code=1).run()
        assert "Failed HTTP/1 transaction with key: cold-child" in cold.output
        self.assert_mismatch_diagnostics(self._cold_child, both_blocks=False)

        parent_diags = self._parent.diags_log.read_text(errors="replace")
        assert "logSliceError" not in parent_diags
        assert "Mismatch/Bad block Content-Range" not in parent_diags
        self.assert_parent_bypass()


def test_slice_stale_generation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A fresh reference block consistently serves the stale object generation."""

    SliceStaleGenerationScenario(ats_factory, services).run()


def test_slice_mixed_generation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A parent-side generation mix breaks both warm and cold child caches."""

    SliceMixedGenerationScenario(ats_factory, services).run()
