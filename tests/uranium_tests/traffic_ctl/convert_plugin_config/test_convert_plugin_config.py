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

from tools.uranium.services import ATS, ATSFactory, assert_matches_gold


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Create the ATS environment used to invoke traffic_ctl.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    return ats_factory.create("ts", enable_cache=False)


def convert(source: str, gold: str, *options: str, output: str = "-", _ats: ATS, _source: Path) -> None:
    """Convert one input and compare it with its wildcard gold file.

    :param _ats: Test-local ats configured by the test.
    :param _source: Test-local source configured by the test.
    :param source: Source used by this test step.
    :param gold: Gold used by this test step.
    :param output: Output used by this test step.
    :param options: Options used by this test step.
    """

    result = _ats.traffic_ctl(
        "config",
        "convert",
        "plugin_config",
        *options,
        str(_source / "legacy_config" / source),
        output,
    )
    assert result.returncode == 0, result.output
    actual = result.stdout if output == "-" else (_ats.run_directory / output).read_text()
    assert_matches_gold(actual, _source / "gold" / gold)


def test_convert_plugin_config(ats_factory: ATSFactory) -> None:
    """traffic_ctl converts all supported plugin.config forms.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _source = Path(__file__).parent
    _ats = configure_ats(ats_factory)

    _ats.start()
    convert("basic.config", "basic.yaml", _ats=_ats, _source=_source)
    convert("commented.config", "commented.yaml", _ats=_ats, _source=_source)
    convert("quoted.config", "quoted.yaml", _ats=_ats, _source=_source)
    convert("basic.config", "basic.yaml", output="generated.yaml", _ats=_ats, _source=_source)
    convert("commented.config", "skip_disabled.yaml", "--skip-disabled", _ats=_ats, _source=_source)
