'''
Test the traffic_ctl config convert cache command.
'''
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

import pytest

from tools.uranium.services import ATSFactory, assert_matches_gold


@pytest.mark.parametrize(
    "source,output", [
        ("legacy_config/basic.config", "-"),
        ("gold/basic.yaml", "-"),
        ("legacy_config/basic.config", "generated.yaml"),
        ("legacy_config/basic.config", "/dev/full"),
    ])
def test_convert_cache(ats_factory: ATSFactory, source: str, output: str) -> None:
    """Convert cache rules with correct output and semantic-change warnings.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param source: Test-relative input configuration filename.
    :param output: Output filename, stdout marker, or failing device path.
    """

    if output == "/dev/full" and not Path(output).exists():
        pytest.skip("/dev/full is required to test output write failures")
    directory = Path(__file__).parent
    ats = ats_factory.create("ts", enable_cache=False)
    ats.start()
    result = ats.traffic_ctl("config", "convert", "cache", str(directory / source), output)
    assert result.returncode == 0, result.output
    warning = "cache.config can combine actions"
    if source.endswith(".config"):
        assert warning in result.stderr, result.output
    else:
        assert warning not in result.stderr, result.output
    if output == "/dev/full":
        assert "Failed to write output file '/dev/full'" in result.stdout, result.output
    else:
        actual = result.stdout if output == "-" else (ats.run_directory / output).read_text()
        assert_matches_gold(actual, directory / "gold/basic.yaml")
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
