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

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(suffix: str, pipe_name: str, pipe_size: int | None = None, *, _ats_factory: ATSFactory) -> ATS:
    """Configure one named-pipe log object.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param suffix: Suffix used by this test step.
    :param pipe_name: Pipe name used by this test step.
    :param pipe_size: Requested pipe capacity in bytes, or None for the default.
    """

    ats = _ats_factory.create(f"ts-{suffix}", disable_log_checks=pipe_size is not None)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "log-file",
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line("map / http://www.linkedin.com/ @action=deny")
    log = {"filename": pipe_name, "mode": "ascii_pipe", "format": "custom"}
    if pipe_size is not None:
        log["pipe_buffer_size"] = pipe_size
    ats.set_logging_yaml({"logging": {
        "formats": [{
            "name": "custom",
            "format": "%<hii> %<hiih>"
        }],
        "logs": [log],
    }})
    return ats


def configure_reader(suffix: str, pipe_path: Path, output_path: Path, *, _services: ServiceFactory) -> ProcessService:
    """Create a long-running FIFO reader.

    :param _services: Test-local services configured by the test.
    :param suffix: Suffix used by this test step.
    :param pipe_path: Path to the pipe file.
    :param output_path: Path to the output file.
    """

    return _services.process(
        f"reader-{suffix}",
        ("/bin/sh", "-c", f"cat '{pipe_path}' > '{output_path}'"),
    )


def generate_log(ats: ATS, reader: ProcessService, output_path: Path, *, _curl: Curl) -> None:
    """Read the pipe while one denied request emits an access entry.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param reader: Reader used by this test step.
    :param output_path: Path to the output file.
    """

    reader.start()
    result = _curl.get(ats, "/", options=f"--verbose")
    assert result.returncode == 0, result.output
    wait_for_file_lines(output_path, "127.0.0.1", 1)


def assert_pipe_diagnostics(ats: ATS, pipe_name: str, *, resized: bool) -> None:
    """Verify pipe creation, no-reader detection, and resize diagnostics.

    :param ats: Traffic Server instance configured or queried by this step.
    :param pipe_name: Pipe name used by this test step.
    :param resized: Resized used by this test step.
    """

    output = ats.traffic_out.read_text(errors="replace")
    assert re.search(rf"Created named pipe .*{re.escape(pipe_name)}", output)
    assert re.search(rf"no readers for pipe .*{re.escape(pipe_name)}", output)
    if resized:
        assert re.search(rf"Previous buffer size for pipe .*{re.escape(pipe_name)}", output)
        assert re.search(rf"New buffer size for pipe.*{re.escape(pipe_name)}", output)
    else:
        assert "New buffer size for pipe" not in output


def run_default_case(*, _ats_factory: ATSFactory, _curl: Curl, _services: ServiceFactory) -> None:
    """Verify the default FIFO size is left untouched.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    pipe_name = "default_pipe_size.pipe"
    ats = configure_ats("default", pipe_name, _ats_factory=_ats_factory)
    ats.start()
    pipe_path = ats.log_directory / pipe_name
    output_path = ats.log_directory / "reader_output"
    reader = configure_reader("default", pipe_path, output_path, _services=_services)
    generate_log(ats, reader, output_path, _curl=_curl)
    assert_pipe_diagnostics(ats, pipe_name, resized=False)


def run_resized_case(*, _ats_factory: ATSFactory, _curl: Curl, _services: ServiceFactory) -> None:
    """Verify an explicit FIFO size is applied or cleanly denied by the kernel.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    pipe_name = "change_pipe_size.pipe"
    pipe_size = 75000
    ats = configure_ats("resized", pipe_name, pipe_size, _ats_factory=_ats_factory)
    ats.start()
    pipe_path = ats.log_directory / pipe_name
    output_path = ats.log_directory / "reader_output"
    reader = configure_reader("resized", pipe_path, output_path, _services=_services)
    generate_log(ats, reader, output_path, _curl=_curl)
    assert_pipe_diagnostics(ats, pipe_name, resized=True)

    verification = ats.run(
        sys.executable,
        TEST_DIRECTORY / "pipe_buffer_is_larger_than.py",
        pipe_path,
        str(pipe_size),
        ats.diags_log,
    )
    assert verification.returncode == 0, verification.output
    assert "Success" in verification.output
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert "FATAL:" not in diagnostics
    assert re.search(r"ERROR:(?! Set pipe size failed for pipe .*: Operation not permitted)", diagnostics) is None


def test_log_pipe(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ASCII pipe logs are readable with default and configured capacities.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if not ats_factory.has_feature("TS_HAS_PIPE_BUFFER_SIZE_CONFIG"):
        pytest.skip("ATS was built without pipe buffer size configuration")

    run_default_case(_ats_factory=ats_factory, _curl=curl, _services=services)
    run_resized_case(_ats_factory=ats_factory, _curl=curl, _services=services)
