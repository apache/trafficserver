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
import re
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
SLICE_STALE_GENERATION_SERVER_REPLAY = TEST_DIRECTORY / "replay" / "slice_stale_generation_server.replay.yaml"
SLICE_STALE_GENERATION_CLIENT_REPLAY = TEST_DIRECTORY / "replay" / "slice_stale_generation_client.replay.yaml"

SLICE_STALE_GENERATION_BLOCK_BYTES = 16


def slice_hierarchy_configure_dns(*, _name: str, _services: ServiceFactory) -> DNSServer:
    """Resolve the logical origin name to loopback.

    :param _name: Test-local name configured by the test.
    :param _services: Test-local services configured by the test.
    """

    return _services.dns(f"dns-{_name}", default="127.0.0.1")


def slice_hierarchy_configure_origin(*, _name: str, _services: ServiceFactory) -> VerifierServer:
    """Key replay responses by URL, byte range, and phase UUID.

    :param _name: Test-local name configured by the test.
    :param _services: Test-local services configured by the test.
    """

    return _services.verifier_server(
        f"origin-{_name}",
        SLICE_STALE_GENERATION_SERVER_REPLAY,
        other_args='--format "{url}{field.range}{field.uuid}"',
    )


def slice_hierarchy_slice_remap(source: str, upstream: str) -> str:
    """Build a remap rule with slice before cache_range_requests.

    :param source: Source used by this test step.
    :param upstream: Upstream used by this test step.
    """

    return (
        f"map {source} {upstream} @plugin=slice.so @pparam=--blockbytes-test={SLICE_STALE_GENERATION_BLOCK_BYTES} "
        "@plugin=cache_range_requests.so")


def slice_hierarchy_configure_records(ats: ATS, *, _dns: DNSServer) -> None:
    """Apply records shared by both cache tiers.

    :param _dns: Test-local dns configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "slice|cache_range_requests",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.parent_proxy.self_detect": 0,
        })


def slice_hierarchy_require_plugins(ats: ATS) -> None:
    """Skip unless the hierarchy plugins and diagnostic plugin are installed.

    :param ats: Traffic Server instance configured or queried by this step.
    """

    required = ("slice.so", "cache_range_requests.so", "xdebug.so")
    if not all(ats.plugin_exists(plugin) for plugin in required):
        pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")


def slice_hierarchy_configure_parent(*, _ats_factory: ATSFactory, _dns: DNSServer, _name: str, _origin: VerifierServer) -> ATS:
    """Configure the cache that stores independent per-Range objects.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _name: Test-local name configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    parent = _ats_factory.create(f"parent-{_name}")
    slice_hierarchy_require_plugins(parent)
    parent.remap_config.add_line(slice_hierarchy_slice_remap("http://origin.test/", f"http://127.0.0.1:{_origin.http_port}/"))
    parent.plugin_config.add_line("xdebug.so --enable=x-cache")
    slice_hierarchy_configure_records(parent, _dns=_dns)
    return parent


def slice_hierarchy_configure_child(label: str, *, _ats_factory: ATSFactory, _dns: DNSServer, _name: str, _parent: ATS) -> ATS:
    """Configure a child that slices requests and forwards blocks to the parent.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _name: Test-local name configured by the test.
    :param _parent: Test-local parent configured by the test.
    :param label: Label used by this test step.
    """

    child = _ats_factory.create(f"{label}-{_name}")
    child.remap_config.add_line(slice_hierarchy_slice_remap("http://slice/", "http://origin.test/"))
    child.parent_config.add_line(f"dest_domain=. parent=127.0.0.1:{_parent.http_port} round_robin=consistent_hash go_direct=false")
    child.plugin_config.add_line("xdebug.so --enable=x-cache")
    slice_hierarchy_configure_records(child, _dns=_dns)
    return child


def slice_hierarchy_configure_client(
        phase: str,
        *,
        ats: ATS | None = None,
        expected_return_code: int = 0,
        _child: ATS,
        _name: str,
        _services: ServiceFactory) -> ProcessService:
    """Create a phase-selected replay client for one child.

    :param _child: Test-local child configured by the test.
    :param _name: Test-local name configured by the test.
    :param _services: Test-local services configured by the test.
    :param phase: Phase used by this test step.
    :param ats: Traffic Server instance configured or queried by this step.
    :param expected_return_code: Expected return code for this case.
    """

    target = _child if ats is None else ats
    return _services.verifier_client(
        f"client-{phase}-{_name}",
        SLICE_STALE_GENERATION_CLIENT_REPLAY,
        http_ports=[target.http_port],
        keys=phase,
        return_code=expected_return_code,
        allow_errors=expected_return_code != 0,
    )


def slice_hierarchy_start_hierarchy(*, _child: ATS, _dns: DNSServer, _origin: VerifierServer, _parent: ATS) -> None:
    """Start DNS, origin, parent, and the primary child in dependency order.

    :param _child: Test-local child configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _parent: Test-local parent configured by the test.
    """

    _dns.start()
    _origin.start()
    _parent.start()
    _child.start()


def slice_hierarchy_assert_parent_bypass(*, _parent: ATS) -> None:
    """Verify child block requests bypass slice on the parent.

    :param _parent: Test-local parent configured by the test.
    """

    output = _parent.traffic_out.read_text(errors="replace")
    assert "slice passing GET or HEAD request through to next plugin" in output
    assert "slice accepting and slicing" not in output


def slice_mixed_generation_assert_mismatch_diagnostics(ats: ATS, *, both_blocks: bool) -> None:
    """Verify the child reports the expected Content-Range identity mismatch.

    :param ats: Traffic Server instance configured or queried by this step.
    :param both_blocks: Both blocks used by this test step.
    """

    content = wait_for_file_lines(ats.diags_log, "Mismatch/Bad block Content-Range", 1)
    assert 'blk_range="16-31"' in content
    assert 'etag_got="%22v1%22"' in content
    if both_blocks:
        content = wait_for_file_lines(ats.diags_log, "Mismatch/Bad block Content-Range", 2)
        assert 'blk_range="0-15"' in content
        assert 'etag_got="%22v2%22"' in content


def test_slice_stale_generation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A fresh reference block consistently serves the stale object generation.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    name = "stale"
    _dns = slice_hierarchy_configure_dns(_name=name, _services=services)
    _origin = slice_hierarchy_configure_origin(_name=name, _services=services)
    _parent = slice_hierarchy_configure_parent(_ats_factory=ats_factory, _dns=_dns, _name=name, _origin=_origin)
    _child = slice_hierarchy_configure_child("child", _ats_factory=ats_factory, _dns=_dns, _name=name, _parent=_parent)

    slice_hierarchy_start_hierarchy(_child=_child, _dns=_dns, _origin=_origin, _parent=_parent)
    for phase in ("fill", "clipped", "unsatisfiable", "control"):
        result = slice_hierarchy_configure_client(phase, _child=_child, _name=name, _services=services).run()
        assert result.returncode == 0, result.output

    origin_output = _origin.output
    for key in ("/objbytes=0-15clipped", "/objbytes=0-15unsatisfiable"):
        assert f"request with key {key}" not in origin_output
    child_diags = _child.diags_log.read_text(errors="replace")
    assert "logSliceError" not in child_diags
    assert "Mismatch/Bad block Content-Range" not in child_diags
    slice_hierarchy_assert_parent_bypass(_parent=_parent)


def test_slice_mixed_generation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A parent-side generation mix breaks both warm and cold child caches.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    name = "mixed"
    _dns = slice_hierarchy_configure_dns(_name=name, _services=services)
    _origin = slice_hierarchy_configure_origin(_name=name, _services=services)
    _parent = slice_hierarchy_configure_parent(_ats_factory=ats_factory, _dns=_dns, _name=name, _origin=_origin)
    _child = slice_hierarchy_configure_child("child", _ats_factory=ats_factory, _dns=_dns, _name=name, _parent=_parent)
    _cold_child = slice_hierarchy_configure_child("cold-child", _ats_factory=ats_factory, _dns=_dns, _name=name, _parent=_parent)

    slice_hierarchy_start_hierarchy(_child=_child, _dns=_dns, _origin=_origin, _parent=_parent)
    fill = slice_hierarchy_configure_client("fill-interior", _child=_child, _name=name, _services=services).run()
    assert fill.returncode == 0, fill.output

    time.sleep(2)
    mixed = slice_hierarchy_configure_client("mixed", expected_return_code=1, _child=_child, _name=name, _services=services).run()
    # As in the original scenario, the abort may reach the client before
    # or after the response header. Neither permits a complete response.
    assert re.search(
        r"Failed to find a well-formed, completed HTTP response: PARSE_INCOMPLETE|"
        r"Content-Length body underrun for key mixed",
        mixed.output,
    )
    assert "Failed HTTP/1 transaction with key: mixed" in mixed.output
    slice_mixed_generation_assert_mismatch_diagnostics(_child, both_blocks=True)

    _cold_child.start()
    cold = slice_hierarchy_configure_client(
        "cold-child", ats=_cold_child, expected_return_code=1, _child=_child, _name=name, _services=services).run()
    assert "Failed HTTP/1 transaction with key: cold-child" in cold.output
    slice_mixed_generation_assert_mismatch_diagnostics(_cold_child, both_blocks=False)

    parent_diags = _parent.diags_log.read_text(errors="replace")
    assert "logSliceError" not in parent_diags
    assert "Mismatch/Bad block Content-Range" not in parent_diags
    slice_hierarchy_assert_parent_bypass(_parent=_parent)
