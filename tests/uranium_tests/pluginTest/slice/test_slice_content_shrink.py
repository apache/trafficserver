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
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory, *, _origin_port: int) -> ProcessService:
    """Start the origin that changes length and ETag between slice fetches.

    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "shrink_origin.py", str(_origin_port)),
        ready_port=_origin_port,
    )


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Configure the slice plugin with seven-byte test blocks.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("slice.so"):
        pytest.skip("slice.so is required")
    ats.remap_config.add_line(
        f"map http://slice/ http://127.0.0.1:{_origin_port}/ "
        "@plugin=slice.so @pparam=--blockbytes-test=7")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "slice",
    })
    return ats


def request_range(path: str, byte_range: str, *, _ats: ATS, _curl: Curl) -> CommandResult:
    """Request one range through ATS's forward-proxy listener.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param byte_range: Byte range to request, or whether to send the test range.
    """

    return _curl.run_for(
        _ats,
        (
            f"--silent --dump-header /dev/stdout --output /dev/stderr --proxy 'localhost:{_ats.http_port}' "
            f"'http://slice/{path}' --range '{byte_range}' --write-out '\nSIZE:%{{size_download}}'"),
    )


def verify_empty_response(result: CommandResult) -> None:
    """Require the failed range to expose no response body.

    :param result: Completed command result to validate.
    """

    assert result.returncode in (0, 18), result.output
    assert re.search(r"SIZE:0\b", result.stdout)
    assert result.stderr == ""


def test_slice_content_shrink(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Shrinking content fails cleanly without an unsigned slice-offset underflow.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin_port = services.allocate_port()
    _origin = configure_origin(services, _origin_port=_origin_port)
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)

    _origin.start()
    _ats.start()
    verify_empty_response(request_range("shrink", "14-20", _ats=_ats, _curl=curl))
    second = request_range("shrink_mid", "16-20", _ats=_ats, _curl=curl)
    assert second.returncode in (0, 18), second.output
    wait_for_file_lines(_ats.diags_log, "shrunk below requested range start", 1)
