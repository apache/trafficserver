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
"""Large compressed statistics and aborted intercept cleanup."""
import re
import shutil
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


def test_stats_over_http_compression(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Verify every generated gauge and bound live continuation counts after aborts.

    :param ats_factory: Proxy factory.
    :param services: Statistics client and continuation counter factory.
    """
    directory = Path(__file__).parent
    ats = ats_factory.create("ts", server_args=["--disable_pfreelist"], disable_log_checks=True)
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_lines(["test_metrics.so --count=10000", "stats_over_http.so _stats"])
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "test_metrics|stats_over_http",
            "proxy.config.dump_mem_info_frequency": 1
        })
    ats.start()
    encodings = ["gzip", "deflate"]
    if ats.has_feature("TS_HAS_BROTLI") and shutil.which("brotli"):
        encodings.append("br")
    for stats_format in ("json", "csv", "prometheus", "prometheus_v2"):
        for encoding in encodings:
            result = services.process(
                f"{stats_format}-{encoding}", [
                    sys.executable, directory / "fetch_stats.py",
                    str(ats.http_port), stats_format, f"--encoding={encoding}", "--count=10000"
                ]).run()
            assert result.returncode == 0, result.output
            assert "Verified 10000 test metrics" in result.stdout
    baseline = ats.run_directory / "cont_baseline"
    result = services.process("count-before",
                              [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline, "--save"]).run()
    assert result.returncode == 0, result.output
    result = services.process(
        "reject", [sys.executable, directory / "fetch_stats.py",
                   str(ats.http_port), "json", "--reject", "--encoding=gzip"]).run()
    assert result.returncode == 0, result.output
    assert "Response status 406" in result.stdout
    for encoding in ("identity", "gzip"):
        arguments = ["--encoding=gzip"] if encoding == "gzip" else []
        result = services.process(
            f"reset-{encoding}",
            [sys.executable, directory / "fetch_stats.py",
             str(ats.http_port), "prometheus", "--reset", *arguments]).run()
        assert result.returncode == 0, result.output
    result = services.process("count-after", [sys.executable, directory / "cont_count.py", ats.traffic_out, baseline]).run()
    assert result.returncode == 0, result.output
    assert "no more than the baseline" in result.stdout
    result = services.process(
        "after-aborts",
        [sys.executable, directory / "fetch_stats.py",
         str(ats.http_port), "json", "--encoding=gzip", "--count=10000"]).run()
    assert result.returncode == 0, result.output
    assert "Verified 10000 test metrics" in result.stdout
    ats.stop()
    traffic_out = ats.traffic_out.read_text(errors="replace")
    assert "Created 10000 metrics" in traffic_out
    assert "Intercept finished on TS_EVENT_NET_ACCEPT_FAILED" in traffic_out, "The rejected request should release its intercept."
    assert re.search("Intercept finished on .*TS_EVENT_(VCONN_EOS|ERROR)", traffic_out), \
        "A reset client connection should release its intercept."
