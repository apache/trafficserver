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
import subprocess
import sys

from tools.uranium.services import ProceduralContext, assert_matches_gold


def convert(
        source_name: str,
        output_name: str,
        *options: str,
        expected_return_code: int = 0,
        _converter: Path,
        _directory: Path,
        _run_directory: Path) -> subprocess.CompletedProcess[str]:
    """Convert one source file into this scenario's sandbox.

    :param _converter: Test-local converter configured by the test.
    :param _directory: Test-local directory configured by the test.
    :param _run_directory: Test-local run directory configured by the test.
    :param source_name: Source name used by this test step.
    :param output_name: Output name used by this test step.
    :param expected_return_code: Expected return code for this case.
    :param options: Options used by this test step.
    """

    result = subprocess.run(
        [
            sys.executable,
            _converter,
            "-f",
            _directory / "legacy_config" / source_name,
            "--output",
            _run_directory / output_name,
            "--yaml",
            *options,
        ],
        cwd=_run_directory,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    assert result.returncode == expected_return_code, result.stdout + result.stderr
    return result


def assert_output(output_name: str, gold_name: str, *, _directory: Path, _run_directory: Path) -> None:
    """Compare one generated YAML document with its gold file.

    :param _directory: Test-local directory configured by the test.
    :param _run_directory: Test-local run directory configured by the test.
    :param output_name: Output name used by this test step.
    :param gold_name: Gold name used by this test step.
    """

    assert_matches_gold(
        (_run_directory / output_name).read_text(errors="replace"),
        _directory / "gold" / gold_name,
    )


def test_records_config_to_yaml(procedural_context: ProceduralContext) -> None:
    """The legacy converter produces the expected nested YAML records.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = Path(__file__).parent
    _run_directory = context.run_directory
    _converter = context.runtime.repository_root / "tools/records/convert2yaml.py"

    convert(
        "full_records.config",
        "generated1.yaml",
        "--mute",
        _converter=_converter,
        _directory=_directory,
        _run_directory=_run_directory)
    assert_output("generated1.yaml", "full_records.yaml", _directory=_directory, _run_directory=_run_directory)

    renamed = convert(
        "old_records.config", "generated2.yaml", _converter=_converter, _directory=_directory, _run_directory=_run_directory)
    renamed_gold = (_directory / "gold/renamed_records.gold").read_text(errors="replace").splitlines()
    assert "\n".join(renamed_gold[1:-1]) in renamed.stdout + renamed.stderr
    assert_output("generated2.yaml", "renamed_records.yaml", _directory=_directory, _run_directory=_run_directory)

    override_value = convert(
        "override_value.config",
        "override-value.yaml",
        "-m",
        expected_return_code=1,
        _converter=_converter,
        _directory=_directory,
        _run_directory=_run_directory)
    assert (
        "We cannot continue with 'proxy.config.ssl.client.verify.server.policy' at line '3' "
        "as a value node will be overridden" in override_value.stdout)

    override_map = convert(
        "override_map.config",
        "override-map.yaml",
        "-m",
        expected_return_code=1,
        _converter=_converter,
        _directory=_directory,
        _run_directory=_run_directory)
    assert (
        "We cannot continue with 'proxy.config.ssl.client.verify.server' at line '3' "
        "as an existing YAML map will be overridden." in override_map.stdout)

    convert(
        "no_newline.config",
        "generated3.yaml",
        "--mute",
        _converter=_converter,
        _directory=_directory,
        _run_directory=_run_directory)
    assert_output("generated3.yaml", "no_newline.yaml", _directory=_directory, _run_directory=_run_directory)
