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
"""Negative gauges print as signed values, and --wrap-counters wraps only counters."""
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, ServiceFactory

TEST_METRICS = "plugin.test_metrics.negative=-42 --counter=plugin.test_metrics.large_counter=9223372036854775850"


def prometheus(counter: str) -> list[list[str]]:
    """The TYPE line and the sample of each test metric, which both Prometheus formats write one after the other.

    :param counter: The value that the large counter prints.
    """
    return [
        ["# TYPE plugin_test_metrics_negative gauge", "plugin_test_metrics_negative -42"],
        ["# TYPE plugin_test_metrics_large_counter counter", f"plugin_test_metrics_large_counter {counter}"],
    ]


def start_ats(ats_factory: ATSFactory, name: str, options: str) -> ATS:
    """Start an ATS with the test metrics and the global stats_over_http plugin.

    :param ats_factory: Proxy factory.
    :param name: The name of the ATS instance.
    :param options: The arguments of stats_over_http.so.
    """
    ats = ats_factory.create(name)
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_lines([f"test_metrics.so {TEST_METRICS}", f"stats_over_http.so {options}"])
    ats.start()
    return ats


def verify(services: ServiceFactory, ats: ATS, name: str, expected: dict[str, list[list[str]]]) -> None:
    """Check that the stats in each format have each group of lines, one line after the other.

    The response of the global plugin is chunked, so fetch_stats.py checks the decoded body.  A chunk boundary can split a
    line of the raw body.

    :param services: Statistics client factory.
    :param ats: The ATS to fetch from.
    :param name: A prefix for the client process names.
    :param expected: For each format, the groups of lines that its body has.
    """
    client = Path(__file__).parent / "fetch_stats.py"
    for stats_format, groups in expected.items():
        arguments = [sys.executable, client, str(ats.http_port), stats_format]
        for group in groups:
            arguments += ["--expect", *group]
        result = services.process(f"{name}-{stats_format}", arguments).run()
        assert result.returncode == 0, result.output
        for group in groups:
            assert f"Found the lines {group}" in result.stdout, f"The decoded {stats_format} body should have {group}"


def test_stats_over_http_signed_gauges(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Print a negative gauge as a signed value and a counter above INT64_MAX unchanged.

    :param ats_factory: Proxy factory.
    :param services: Statistics client factory.
    """
    ats = start_ats(ats_factory, "ts", "_stats")
    verify(
        services, ats, "signed", {
            "json": [['"plugin.test_metrics.negative": "-42",'], ['"plugin.test_metrics.large_counter": "9223372036854775850",']],
            "csv": [["plugin.test_metrics.negative,-42"], ["plugin.test_metrics.large_counter,9223372036854775850"]],
            "prometheus": prometheus("9223372036854775850"),
            "prometheus_v2": prometheus("9223372036854775850"),
        })


def test_stats_over_http_wrap_counters(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """With --wrap-counters and --integer-counters, wrap a counter above INT64_MAX and print a negative gauge as a signed value.

    :param ats_factory: Proxy factory.
    :param services: Statistics client factory.
    """
    ats = start_ats(ats_factory, "ts_wrap", "--wrap-counters --integer-counters _stats")
    verify(
        services, ats, "wrap", {
            "json": [['"plugin.test_metrics.negative": -42,'], ['"plugin.test_metrics.large_counter": 43,']],
            "csv": [["plugin.test_metrics.negative,-42"], ["plugin.test_metrics.large_counter,43"]],
            "prometheus": prometheus("43"),
            "prometheus_v2": prometheus("43"),
        })
