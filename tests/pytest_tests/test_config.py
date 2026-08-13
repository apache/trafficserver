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
"""Unit tests for replay metadata parsing."""

from pathlib import Path

import pytest
import yaml

from ats_testkit.config import ReplayConfigError, ReplaySpec, merge_flat_records, replace_server_ports


def test_all_migrated_replays_are_valid() -> None:
    """Verify every directly collected replay has valid test metadata."""

    gold_tests = Path(__file__).parents[1] / "gold_tests"
    replay_files = list(gold_tests.rglob("*.test.yaml"))
    assert len(replay_files) == 96
    for path in replay_files:
        ReplaySpec.load(path)


def test_no_gold_test_registers_an_ats_replay() -> None:
    """Keep replay ownership out of legacy Python wrappers."""

    gold_tests = Path(__file__).parents[1] / "gold_tests"
    registrations = [path for path in gold_tests.rglob("*.test.py") if "Test.ATSReplayTest(" in path.read_text()]
    assert registrations == []


def test_replay_requires_autest_metadata(tmp_path: Path) -> None:
    """Reject a Proxy Verifier file that has not opted into direct collection."""

    path = tmp_path / "missing.test.yaml"
    path.write_text(yaml.safe_dump({"meta": {"version": "1.0"}, "sessions": []}))
    with pytest.raises(ReplayConfigError, match="'autest' mapping"):
        ReplaySpec.load(path)


def test_replay_accepts_summary_as_description(tmp_path: Path) -> None:
    """Keep compatibility with the replay-only AuTest YAML format."""

    path = tmp_path / "summary.test.yaml"
    path.write_text(
        yaml.safe_dump({
            "autest": {
                "summary": "Summary-only replay",
                "server": {},
                "client": {},
                "ats": {},
            },
            "sessions": [],
        }))
    assert ReplaySpec.load(path).description == "Summary-only replay"


def test_flat_records_merge_with_nested_defaults() -> None:
    """Build the nested records.yaml shape ATS expects."""

    result = merge_flat_records(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.http.cache.http": 0,
        },
        {"http": {
            "server_ports": "8080"
        }},
    )
    assert result == {
        "records": {
            "http": {
                "server_ports": "8080",
                "cache": {
                    "http": 0
                }
            },
            "diags": {
                "debug": {
                    "enabled": 1
                }
            },
        }
    }


def test_server_port_substitution() -> None:
    """Substitute both Proxy Verifier origin listener placeholders."""

    assert replace_server_ports("http://host:{SERVER_HTTP_PORT}/{SERVER_HTTPS_PORT}", 1234, 5678) == "http://host:1234/5678"
