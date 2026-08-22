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
import ast

import pytest
import yaml

from tools.uranium.config import ReplayConfigError, ReplaySpec, merge_flat_records, replace_server_ports


def test_all_migrated_replays_are_valid() -> None:
    """Verify every directly collected replay has valid test metadata."""

    uranium_tests = Path(__file__).parents[3] / "uranium_tests"
    replay_files = list(uranium_tests.rglob("*.test.yaml"))
    assert len(replay_files) >= 119
    for path in replay_files:
        ReplaySpec.load_all(path)


def test_default_replays_are_self_contained() -> None:
    """Keep default Proxy Verifier traffic in its collected manifest."""

    uranium_tests = Path(__file__).parents[3] / "uranium_tests"
    violations = []
    for path in uranium_tests.rglob("*.test.yaml"):
        document = yaml.safe_load(path.read_text())
        urtest = document["urtest"]
        replay = urtest.get("replay")
        if not isinstance(replay, str):
            continue

        base_replay_path = (path.parent / replay).resolve()
        if base_replay_path.is_file():
            violations.append(f"{path}: merge {base_replay_path} into the collected manifest")

    assert violations == []


def test_no_uranium_test_registers_an_uranium_replay() -> None:
    """Keep replay ownership out of legacy Python wrappers."""

    uranium_tests = Path(__file__).parents[3] / "uranium_tests"
    registrations = [path for path in uranium_tests.rglob("test_*.py") if "Test.ATSReplayTest(" in path.read_text()]
    assert registrations == []


def test_all_bespoke_tests_are_available_to_pytest() -> None:
    """Keep the native procedural-test inventory explicit."""

    uranium_tests = Path(__file__).parents[3] / "uranium_tests"
    assert len(list(uranium_tests.rglob("test_*.py"))) == 304


def test_curl_call_sites_use_shell_style_strings() -> None:
    """Keep native curl calls on the readable string-based API."""

    uranium_tests = Path(__file__).parents[3] / "uranium_tests"
    violations = []
    for path in uranium_tests.rglob("*.py"):
        tree = ast.parse(path.read_text())
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
                continue
            if not _is_curl_receiver(node.func.value):
                continue
            if node.func.attr == "run_for" and len(node.args) != 2:
                violations.append(f"{path}:{node.lineno}: run_for expects ATS and one argument string")
            if node.func.attr == "run" and len(node.args) != 1:
                violations.append(f"{path}:{node.lineno}: run expects one argument string")
            if node.func.attr == "get":
                options = next((keyword.value for keyword in node.keywords if keyword.arg == "options"), None)
                if isinstance(options, (ast.List, ast.Tuple)):
                    violations.append(f"{path}:{node.lineno}: get options must be a string")
    assert violations == []


def _is_curl_receiver(node: ast.expr) -> bool:
    """Return whether an attribute chain identifies a Curl fixture.

    :param node: Expression to inspect for a ``curl`` or ``_curl`` component.
    """

    while isinstance(node, ast.Attribute):
        if node.attr in {"curl", "_curl"}:
            return True
        node = node.value
    return isinstance(node, ast.Name) and node.id in {"curl", "_curl"}


def test_replay_requires_urtest_metadata(tmp_path: Path) -> None:
    """Reject a Proxy Verifier file that has not opted into direct collection."""

    path = tmp_path / "missing.test.yaml"
    path.write_text(yaml.safe_dump({"meta": {"version": "1.0"}, "sessions": []}))
    with pytest.raises(ReplayConfigError, match="'urtest' mapping"):
        ReplaySpec.load(path)


def test_replay_accepts_summary_as_description(tmp_path: Path) -> None:
    """Accept summary as an alternative replay description."""

    path = tmp_path / "summary.test.yaml"
    path.write_text(
        yaml.safe_dump({
            "urtest": {
                "summary": "Summary-only replay",
                "server": {},
                "client": {},
                "ats": {},
            },
            "sessions": [],
        }))
    assert ReplaySpec.load(path).description == "Summary-only replay"


def test_replay_requires_ats_environment_mapping(tmp_path: Path) -> None:
    """Reject ATS environment metadata that cannot become process variables."""

    path = tmp_path / "environment.test.yaml"
    path.write_text(
        yaml.safe_dump(
            {
                "urtest":
                    {
                        "description": "Invalid ATS environment",
                        "server": {},
                        "client": {},
                        "ats": {
                            "environment": ["ATS_TEST_HOOK=1"]
                        },
                    },
                "sessions": [],
            }))
    with pytest.raises(ReplayConfigError, match="ats.environment"):
        ReplaySpec.load(path)


def test_replay_manual_metadata_requires_boolean_or_reason(tmp_path: Path) -> None:
    """Reject manual metadata that cannot become a pytest marker.

    :param tmp_path: Temporary directory in which to write the replay.
    """

    path = tmp_path / "manual.test.yaml"
    path.write_text(
        yaml.safe_dump(
            {
                "urtest": {
                    "description": "Invalid manual metadata",
                    "manual": ["slow"],
                    "server": {},
                    "client": {},
                    "ats": {},
                },
                "sessions": [],
            }))
    with pytest.raises(ReplayConfigError, match="urtest.manual"):
        ReplaySpec.load(path)


def test_replay_manifest_resolves_traffic_file(tmp_path: Path) -> None:
    """Resolve a manifest's traffic replay relative to the manifest."""

    traffic = tmp_path / "traffic.yaml"
    traffic.write_text("meta: {version: '1.0'}\nsessions: []\n")
    path = tmp_path / "manifest.test.yaml"
    path.write_text(
        yaml.safe_dump(
            {"urtest": {
                "description": "Replay manifest",
                "replay": traffic.name,
                "server": {},
                "client": {},
                "ats": {},
            }}))

    assert ReplaySpec.load(path).replay_path == traffic


def test_replay_manifest_requires_traffic_file(tmp_path: Path) -> None:
    """Reject a manifest whose traffic replay is missing."""

    path = tmp_path / "manifest.test.yaml"
    path.write_text(
        yaml.safe_dump(
            {"urtest": {
                "description": "Missing replay",
                "replay": "missing.yaml",
                "server": {},
                "client": {},
                "ats": {},
            }}))

    with pytest.raises(ReplayConfigError, match="replay file does not exist"):
        ReplaySpec.load(path)


def test_replay_manifest_variants_are_merged(tmp_path: Path) -> None:
    """Collect named variants with recursively merged ATS metadata."""

    traffic = tmp_path / "traffic.yaml"
    traffic.write_text("meta: {version: '1.0'}\nsessions: []\n")
    path = tmp_path / "variants.test.yaml"
    path.write_text(
        yaml.safe_dump(
            {
                "urtest":
                    {
                        "description": "Variant replay",
                        "replay": traffic.name,
                        "server": {},
                        "client": {},
                        "ats": {
                            "records_config": {
                                "one": 1
                            }
                        },
                        "variants":
                            [
                                {
                                    "name": "two",
                                    "ats": {
                                        "records_config": {
                                            "two": 2
                                        }
                                    },
                                },
                                {
                                    "name": "three",
                                    "ats": {
                                        "records_config": {
                                            "two": 3
                                        }
                                    },
                                },
                            ],
                    }
            }))

    specs = ReplaySpec.load_all(path)

    assert [spec.variant_name for spec in specs] == ["two", "three"]
    assert specs[0].urtest["ats"]["records_config"] == {"one": 1, "two": 2}
    assert specs[1].urtest["ats"]["records_config"] == {"one": 1, "two": 3}


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
