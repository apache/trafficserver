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
"""Serve many metrics from stats_over_http remap rules and release each request."""
import re
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, wait_for_file_lines

METRIC_COUNT = 10000


def fetch(services: ServiceFactory, ats: ATS, name: str, stats_format: str) -> None:
    """Fetch the stats in one format and check that its remap rule serves every test metric.

    :param services: Statistics client factory.
    :param ats: The ATS to fetch from.
    :param name: The client process name.
    :param stats_format: The format, which selects the remap rule.
    """
    result = services.process(
        name,
        [sys.executable,
         Path(__file__).parent / "fetch_stats.py",
         str(ats.http_port), stats_format, f"--count={METRIC_COUNT}"]).run()
    assert result.returncode == 0, result.output
    assert f"Verified {METRIC_COUNT} test metrics" in result.stdout, f"The {stats_format} body should hold every test metric."
    assert re.search(rf"^X-Stats-Format: {stats_format}$", result.stdout, re.MULTILINE), \
        f"The {stats_format} rule should serve the request."


def test_stats_over_http_remap_intercept(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Verify that remap rules serve many metrics and that each request releases its continuations.

    :param ats_factory: Proxy factory.
    :param services: Statistics client and continuation counter factory.
    :param curl: Tells whether the run uses Unix domain sockets.
    """
    if curl.uses_uds:
        pytest.skip("fetch_stats.py connects over TCP")
    directory = Path(__file__).parent
    # LeakSanitizer does not run on macOS, so cont_count.py counts live continuations to detect a leaked intercept or
    # transaction hook.
    ats = ats_factory.create("ts", server_args=["--disable_pfreelist"])
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_line(f"test_metrics.so --count={METRIC_COUNT}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "stats_over_http",
            "proxy.config.dump_mem_info_frequency": 1,
        })
    # A remap rule matches a path prefix, so the prometheus_v2 rule comes before the prometheus rule.
    for stats_format in ["prometheus_v2", "prometheus", "json", "csv"]:
        ats.remap_config.add_line(
            f"map http://127.0.0.1:{ats.http_port}/_stats/{stats_format} http://127.0.0.1/"
            f" @plugin=stats_over_http.so @pparam=--format={stats_format}")
    ats.start()

    for stats_format in ["json", "csv", "prometheus", "prometheus_v2"]:
        fetch(services, ats, f"fetch-{stats_format}", stats_format)

    client = directory / "fetch_stats.py"
    baseline = ats.run_directory / "cont_baseline"
    result = services.process("count-before",
                              [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline, "--save"]).run()
    assert result.returncode == 0, result.output

    fetch(services, ats, "fetch-again", "prometheus")
    for name, option, status in [("head", "--head", 200), ("post", "--post", 405), ("reject", "--reject", 406)]:
        result = services.process(name, [sys.executable, client, str(ats.http_port), "prometheus", option]).run()
        assert result.returncode == 0, result.output
        assert f"Response status {status}" in result.stdout, f"The {name} request should get a {status}."
    result = services.process("reset", [sys.executable, client, str(ats.http_port), "prometheus", "--reset"]).run()
    assert result.returncode == 0, result.output

    result = services.process("count-after", [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline]).run()
    assert result.returncode == 0, result.output
    assert "no more than the baseline" in result.stdout, "The requests should release their continuations."

    fetch(services, ats, "fetch-after-aborts", "json")
    wait_for_file_lines(ats.traffic_out, "Intercept finished on TS_EVENT_NET_ACCEPT_FAILED", 1)
    wait_for_file_lines(ats.traffic_out, "Intercept finished on .*TS_EVENT_(VCONN_EOS|ERROR)", 1)
