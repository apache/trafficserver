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
"""Render stats on a task thread while requests wait for the render."""
import re
import shutil
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory

METRIC_COUNT = 1000
REUSE_MAX_AGE_MS = 3000
REMAP_WAIT_TIMEOUT_MS = 1000
GLOBAL_WAIT_TIMEOUT_MS = 2000


def run(services: ServiceFactory, ats: ATS, scenario: str, path: str = "/", extra: list[str] | None = None) -> None:
    """Run one task_render.py scenario against a Traffic Server.

    :param services: Driver factory.
    :param ats: The ATS that the scenario uses.
    :param scenario: The task_render.py scenario.
    :param path: The path of the stats.
    :param extra: More task_render.py arguments.
    """
    command = [
        sys.executable,
        Path(__file__).parent / "task_render.py",
        str(ats.http_port), ats.traffic_out, scenario, f"--path={path}", f"--count={METRIC_COUNT}",
        f"--remap-config={ats.remap_config.path}", *(extra or [])
    ]
    result = services.process(scenario, command, environment=ats.environment).run()
    assert result.returncode == 0, result.output
    assert f"{scenario}: passed" in result.stdout, f"The {scenario} scenario should pass."


def check_task_thread_renders(ats: ATS) -> None:
    """Check that a stopped ATS rendered the stats only on a task thread.

    :param ats: The stopped ATS.
    """
    output = ats.traffic_out.read_text(errors="replace")
    assert re.search(r"\[ET_TASK \d+\] .*Rendered stats instance", output), \
        f"{ats.name} should render the stats on a task thread."
    assert not re.search(r"^\[[^\]]*\] (?!\[ET_TASK \d+\] ).*Rendered stats instance", output, re.MULTILINE), \
        f"{ats.name} should render the stats only on a task thread."


def test_stats_over_http_task_render(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Verify that the plugin renders stats on a task thread while requests wait for the render.

    :param ats_factory: Proxy factory.
    :param services: Driver and continuation counter factory.
    :param curl: Tells whether the run uses Unix domain sockets.
    """
    if curl.uses_uds:
        pytest.skip("task_render.py connects over TCP")
    directory = Path(__file__).parent

    # LeakSanitizer does not run on macOS, so cont_count.py counts live continuations to detect a leaked render, watchdog,
    # hook or intercept.
    ats = ats_factory.create("ts", server_args=["--disable_pfreelist"])
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_lines([f"test_metrics.so --count={METRIC_COUNT}", "stats_over_http.so _stats"])
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "stats_over_http|test_metrics",
            "proxy.config.dump_mem_info_frequency": 1,
            # A stalled render holds one task thread.  More threads make it likely that a remap reload runs on another.
            "proxy.config.task_threads": 4,
        })
    rules = {
        "concurrent": "@pparam=--format=prometheus",
        "reuse": f"@pparam=--format=json @pparam=--max-age-ms={REUSE_MAX_AGE_MS}",
        "close": "@pparam=--format=json",
        "timeout": f"@pparam=--format=prometheus @pparam=--wait-timeout-ms={REMAP_WAIT_TIMEOUT_MS}",
        "loop": "@pparam=--format=prometheus @pparam=--max-age-ms=0",
    }
    for name, options in rules.items():
        ats.remap_config.add_line(
            f"map http://127.0.0.1:{ats.http_port}/metrics/{name} http://127.0.0.1/ @plugin=stats_over_http.so {options}")
    # The reset scenario expects the intercept to finish before the render, which needs allow_half_open=0.
    ats.remap_config.add_line(
        f"map http://127.0.0.1:{ats.http_port}/metrics/reset http://127.0.0.1/"
        " @plugin=conf_remap.so @pparam=proxy.config.http.allow_half_open=0 @plugin=stats_over_http.so @pparam=--format=json")

    # A process loads the global plugin once, so the global plugin with a short wait timeout needs a process of its own.
    # With one task thread, a held task can delay a render.
    ats_global = ats_factory.create("ts_global")
    ats_global.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats_global.plugin_config.add_lines(
        [
            f"test_metrics.so --count={METRIC_COUNT}",
            f"stats_over_http.so --max-age-ms=0 --wait-timeout-ms={GLOBAL_WAIT_TIMEOUT_MS} _stats",
        ])
    ats_global.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "stats_over_http|test_metrics",
            "proxy.config.task_threads": 1,
        })

    ats.start()
    result = services.process(
        "fetch-before", [sys.executable, directory / "fetch_stats.py",
                         str(ats.http_port), "json", f"--count={METRIC_COUNT}"]).run()
    assert result.returncode == 0, result.output
    baseline = ats.run_directory / "cont_baseline"
    result = services.process("count-before",
                              [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline, "--save"]).run()
    assert result.returncode == 0, result.output

    # Answer concurrent requests from one render.
    run(services, ats, "concurrent", "/metrics/concurrent")
    # Answer requests from one render until it is older than --max-age-ms.
    run(services, ats, "reuse", "/metrics/reuse", [f"--max-age-ms={REUSE_MAX_AGE_MS}"])
    # Reset, then close, a connection while its request waits for a render.
    run(services, ats, "reset", "/metrics/reset")
    run(services, ats, "close", "/metrics/close")
    # Answer the global plugin in many formats and encodings.  task_render.py decodes br with the brotli command.
    run(services, ats, "global", extra=["--brotli"] if ats.has_feature("TS_HAS_BROTLI") and shutil.which("brotli") else [])
    # Time out a request, then reload remap.config while the render runs.
    run(services, ats, "timeout-reload", "/metrics/timeout", [f"--wait-timeout-ms={REMAP_WAIT_TIMEOUT_MS}"])
    # Reload remap.config while requests render.
    run(services, ats, "reload-loop", "/metrics/loop")

    ats_global.start()
    # Time out a request to the global plugin.  Give a request that waits for a second render its own wait timeout.  Answer
    # a request that arrives during a render from the next render.
    for scenario in ["global-timeout", "global-handover", "global-fresh"]:
        run(services, ats_global, scenario, extra=[f"--wait-timeout-ms={GLOBAL_WAIT_TIMEOUT_MS}"])

    result = services.process("count-after", [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline]).run()
    assert result.returncode == 0, result.output
    assert "no more than the baseline" in result.stdout, \
        "The requests, renders and watchdogs should release their continuations."

    ats.stop()
    ats_global.stop()
    check_task_thread_renders(ats)
    check_task_thread_renders(ats_global)
