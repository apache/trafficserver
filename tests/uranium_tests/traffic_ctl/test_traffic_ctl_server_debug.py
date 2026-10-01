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

from tools.uranium.services import ATS, ATSFactory, CommandResult


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Start with debug output disabled and a recognizable tag value.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 0,
        "proxy.config.diags.debug.tags": "xyz",
    })
    return ats


def traffic_ctl(*arguments: str, expected: int = 0, _ats: ATS) -> CommandResult:
    """Run traffic_ctl and validate its status.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    :param arguments: Arguments used by this test step.
    """

    result = _ats.traffic_ctl(*arguments)
    assert result.returncode == expected, result.output
    return result


def assert_record(record: str, value: str, *, _ats: ATS) -> None:
    """Verify one runtime record value.

    :param _ats: Test-local ats configured by the test.
    :param record: Record used by this test step.
    :param value: Value used by this test step.
    """

    result = traffic_ctl("config", "get", record, _ats=_ats)
    assert f"{record}: {value}" in result.stdout


def enable(tags: str, *, append: bool = False, _ats: ATS) -> None:
    """Enable debug output with replacement or append semantics.

    :param _ats: Test-local ats configured by the test.
    :param tags: Tags used by this test step.
    :param append: Append used by this test step.
    """

    arguments = ["server", "debug", "enable", "--tags", tags]
    if append:
        arguments.append("--append")
    traffic_ctl(*arguments, _ats=_ats)


def test_traffic_ctl_server_debug(ats_factory: ATSFactory) -> None:
    """traffic_ctl updates debug records and enforces its option contract.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    enable("http", _ats=_ats)
    assert_record("proxy.config.diags.debug.enabled", "1", _ats=_ats)
    assert_record("proxy.config.diags.debug.tags", "http", _ats=_ats)

    traffic_ctl("server", "debug", "disable", _ats=_ats)
    assert_record("proxy.config.diags.debug.enabled", "0", _ats=_ats)

    enable("cache", _ats=_ats)
    assert_record("proxy.config.diags.debug.tags", "cache", _ats=_ats)
    enable("http", append=True, _ats=_ats)
    assert_record("proxy.config.diags.debug.tags", "cache|http", _ats=_ats)
    enable("dns", append=True, _ats=_ats)
    assert_record("proxy.config.diags.debug.tags", "cache|http|dns", _ats=_ats)

    traffic_ctl("server", "debug", "disable", _ats=_ats)
    assert_record("proxy.config.diags.debug.enabled", "0", _ats=_ats)
    result = traffic_ctl("server", "debug", "enable", "--append", expected=64, _ats=_ats)
    assert "Option '--append' requires '--tags' to be specified" in result.output

    result = traffic_ctl("server", "debug", "enable", "--tags", "--append", expected=64, _ats=_ats)
    assert "1 argument(s) expected by tags" in result.output

    result = traffic_ctl("server", "debug", "enable", "--tags", "--", "-a", _ats=_ats)
    assert 'tags »"-a"«' in result.stdout
