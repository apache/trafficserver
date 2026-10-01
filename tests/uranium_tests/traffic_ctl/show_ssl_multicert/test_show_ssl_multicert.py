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
    """Configure TLS with the default Uranium certificate.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
    ats.add_default_ssl_files()
    return ats


def verify_output(option: str | None, gold: str, *, _ats: ATS, _source: Path) -> None:
    """Run the show command and compare its selected serialization.

    :param _ats: Test-local ats configured by the test.
    :param _source: Test-local source configured by the test.
    :param option: Option used by this test step.
    :param gold: Gold used by this test step.
    """

    arguments = ["config", "ssl-multicert", "show"]
    if option is not None:
        arguments.append(option)
    result = _ats.traffic_ctl(*arguments)
    assert result.returncode == 0, result.output
    assert_matches_gold(result.stdout, _source / "gold" / gold)


def test_show_ssl_multicert(ats_factory: ATSFactory) -> None:
    """ssl-multicert show supports its YAML and JSON spellings.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _source = Path(__file__).parent
    _ats = configure_ats(ats_factory)

    _ats.start()
    for option in (None, "--yaml", "-y"):
        verify_output(option, "show_yaml.gold", _ats=_ats, _source=_source)
    for option in ("--json", "-j"):
        verify_output(option, "show_json.gold", _ats=_ats, _source=_source)
