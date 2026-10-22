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

import pytest

from tools.uranium.services import ATSFactory, Curl, assert_matches_gold


def test_stats_over_http(ats_factory: ATSFactory, curl: Curl) -> None:
    """The stats endpoint returns the branch's JSON statistics and HTTP headers.

    :param ats_factory: Factory for isolated ATS processes.
    :param curl: Transport-aware curl command runner.
    """
    ats = ats_factory.create("ts")
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is not installed")
    if curl.uses_uds:
        pytest.skip("stats_over_http does not support the curl UDS mode")
    ats.plugin_config.add_line("stats_over_http.so _stats")
    ats.start()
    result = curl.get(ats, "/_stats", options="--verbose --silent --http1.1")
    assert result.returncode == 0, result.output
    gold = Path(__file__).parent / "gold"
    assert_matches_gold(result.stdout, gold / "stats_over_http_0_stdout.gold")
    assert_matches_gold(result.stderr, gold / "stats_over_http_0_stderr.gold")
