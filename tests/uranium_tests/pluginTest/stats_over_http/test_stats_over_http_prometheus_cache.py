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
"""Render Prometheus stats from the cache as metrics appear and change."""
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines

# The path, whether its families have HELP lines, and whether it is the v2 format.
ENDPOINTS: list[tuple[str, bool, bool]] = [
    ("/metrics/help/v2", True, True),
    ("/metrics/help/v1", True, False),
    ("/metrics/nohelp/v2", False, True),
    ("/metrics/nohelp/v1", False, False),
    ("/_stats/prometheus_v2", False, True),
    ("/_stats/prometheus", False, False),
]


def scrape(
        services: ServiceFactory,
        ats: ATS,
        name: str,
        path: str,
        help_lines: bool,
        expect: list[str],
        families: list[str],
        wait: bool = False,
        concurrency: int = 1) -> None:
    """Scrape one endpoint and check its samples and families.

    :param services: Scrape client factory.
    :param ats: The ATS to scrape.
    :param name: The client process name.
    :param path: The path to scrape.
    :param help_lines: Whether every family should have a HELP line.
    :param expect: Lines that the body should have.
    :param families: NAME=COUNT, the number of samples of a family.
    :param wait: Whether to fetch again until every expected line is there.
    :param concurrency: The number of requests to send at once.
    """
    arguments = [
        sys.executable,
        Path(__file__).parent / "check_prometheus.py",
        str(ats.http_port), path, f"--concurrency={concurrency}"
    ]
    arguments += ["--help-lines"] if help_lines else []
    arguments += ["--wait"] if wait else []
    arguments += [f"--expect={line}" for line in expect]
    arguments += [f"--family={family}" for family in families]
    result = services.process(name, arguments).run()
    assert result.returncode == 0, result.output
    assert "Verified" in result.stdout, f"The {path} output should be valid."


def test_stats_over_http_prometheus_cache(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Verify the Prometheus output that the plugin renders from its cache as metrics appear and change.

    :param ats_factory: Proxy factory.
    :param services: Scrape client factory.
    """
    ats = ats_factory.create("ts")
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_lines(
        [
            "test_metrics.so --count=2000 plugin.test_metrics.requests.get=1 plugin.test_metrics.queue=4",
            "stats_over_http.so --no-prometheus-help _stats",
        ])
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "test_metrics|stats_over_http",
    })
    for path, options in [
        ("help/v2", "--format=prometheus_v2"),
        ("help/v1", "--format=prometheus"),
        ("nohelp/v2", "--format=prometheus_v2 @pparam=--no-prometheus-help"),
        ("nohelp/v1", "--format=prometheus @pparam=--no-prometheus-help"),
    ]:
        ats.remap_config.add_line(
            f"map http://127.0.0.1:{ats.http_port}/metrics/{path} http://127.0.0.1/"
            f" @plugin=stats_over_http.so @pparam={options}")
    ats.start()

    # The first metrics.
    for i, (path, help_lines, v2) in enumerate(ENDPOINTS):
        if v2:
            expect = ['plugin_test_metrics_requests{method="get"} 1', "plugin_test_metrics_queue 4"]
            families = ["plugin_test_metrics_requests=1"]
            if help_lines:
                expect.append("# HELP plugin_test_metrics_requests plugin.test_metrics.requests.get")
        else:
            expect = ["plugin_test_metrics_requests_get 1", "plugin_test_metrics_queue 4"]
            families = ["plugin_test_metrics_requests_post=0"]
        scrape(services, ats, f"first-{i}", path, help_lines, expect, families)

    # A new sample for an existing family, a new family and a changed value.
    for assignment in ["plugin.test_metrics.requests.post=2", "plugin.test_metrics.late=7", "plugin.test_metrics.requests.get=-5"]:
        result = ats.traffic_ctl("plugin", "msg", "test_metrics", assignment)
        assert result.returncode == 0, result.output
    for i, (path, help_lines, v2) in enumerate(ENDPOINTS):
        if v2:
            expect = [
                'plugin_test_metrics_requests{method="get"} -5', 'plugin_test_metrics_requests{method="post"} 2',
                "plugin_test_metrics_late 7"
            ]
            families = ["plugin_test_metrics_requests=2"]
        else:
            expect = ["plugin_test_metrics_requests_get -5", "plugin_test_metrics_requests_post 2", "plugin_test_metrics_late 7"]
            families = ["plugin_test_metrics_requests_post=1"]
        scrape(services, ats, f"changed-{i}", path, help_lines, expect, families, wait=True)

    # Concurrent requests share a render.  Each body must still be complete.
    for i, (path, help_lines, v2) in enumerate([ENDPOINTS[0], ENDPOINTS[-1]]):
        if v2:
            expect = ['plugin_test_metrics_requests{method="post"} 2', "plugin_test_metrics_late 7"]
            families = ["plugin_test_metrics_requests=2"]
        else:
            expect = ["plugin_test_metrics_requests_post 2", "plugin_test_metrics_late 7"]
            families = ["plugin_test_metrics_requests_post=1"]
        scrape(services, ats, f"concurrent-{i}", path, help_lines, expect, families, concurrency=16)

    # The remap rules reuse a render for 1 s, but a request with records[] gets its own render.  The plugin's count of
    # renders changes with each render, so two responses in a row differ.
    result = services.process(
        "records", [
            sys.executable,
            Path(__file__).parent / "check_prometheus.py",
            str(ats.http_port), "/metrics/nohelp/v1?records%5B%5D=plugin.stats_over_http.renders", "--fresh",
            "--family=plugin_stats_over_http_renders=1"
        ]).run()
    assert result.returncode == 0, result.output
    assert "Verified 1 families and 1 samples" in result.stdout, "The response should hold only the sample of the named record."

    wait_for_file_lines(ats.traffic_out, "Assigned plugin.test_metrics.late=7", 1)


def test_stats_over_http_records_unlisted(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A records[] request omits a metric that ATS has unlisted, as a full scrape does, and serves it again when relisted.

    :param ats_factory: Proxy factory.
    :param services: Scrape client factory.
    """
    ats = ats_factory.create("ts")
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_lines(["test_metrics.so --count=10", "stats_over_http.so --no-prometheus-help _stats"])
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "test_metrics|stats_over_http",
    })
    ats.start()

    def records(name: str, samples: int) -> None:
        result = services.process(
            name, [
                sys.executable,
                Path(__file__).parent / "check_prometheus.py",
                str(ats.http_port), "/_stats/prometheus?records%5B%5D=plugin.test_metrics.gauge_5",
                f"--family=plugin_test_metrics_gauge_5={samples}"
            ]).run()
        assert result.returncode == 0, result.output
        assert f"and {samples} samples" in result.stdout, result.stdout

    def message(tag: str) -> None:
        result = ats.traffic_ctl("plugin", "msg", tag, "plugin.test_metrics.gauge_5")
        assert result.returncode == 0, result.output

    # The metric is listed, so a records[] request has its sample.
    records("listed", 1)
    # Unlisting it takes it out of a full scrape, so a records[] request leaves it out too.
    message("test_metrics.unlist")
    records("unlisted", 0)
    # Listing it again restores the sample.
    message("test_metrics.relist")
    records("relisted", 1)
