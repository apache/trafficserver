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
"""Installed ATS and test-program discovery."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess

import fcntl

from .config import version_tuple


class RuntimeError(ValueError):
    """Report an invalid end-to-end test runtime."""


@dataclass(frozen=True)
class TestRuntime:
    """Paths and build features shared by replay test items."""

    repository_root: Path
    build_root: Path
    ats_bin: Path
    verifier_bin: Path
    sandbox_root: Path
    layout: dict[str, str]
    features: dict[str, Any]

    @classmethod
    def create(
        cls,
        repository_root: Path,
        build_root: Path,
        ats_bin: Path,
        verifier_bin: Path,
        sandbox_root: Path,
    ) -> "TestRuntime":
        """Validate paths and query the installed ATS layout."""

        traffic_layout = ats_bin / "traffic_layout"
        required = [
            traffic_layout,
            ats_bin / "traffic_server",
            ats_bin / "traffic_ctl",
            verifier_bin / "verifier-client",
            verifier_bin / "verifier-server",
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError("Missing required test programs: " + ", ".join(missing))

        try:
            layout = json.loads(subprocess.check_output([traffic_layout, "--json"], text=True))
            features = json.loads(subprocess.check_output([traffic_layout, "--features", "--json"], text=True))
        except (subprocess.CalledProcessError, json.JSONDecodeError) as error:
            raise RuntimeError(f"Could not query {traffic_layout}: {error}") from error

        return cls(
            repository_root=repository_root.resolve(),
            build_root=build_root.resolve(),
            ats_bin=ats_bin.resolve(),
            verifier_bin=verifier_bin.resolve(),
            sandbox_root=sandbox_root.resolve(),
            layout={
                key: value.rstrip("/") for key, value in layout.items()
            },
            features=features,
        )

    @property
    def test_tools(self) -> Path:
        """Return the source test-tools directory."""

        return self.repository_root / "tests" / "tools"

    @property
    def build_gold_tests(self) -> Path:
        """Return the build-tree mirror of gold_tests."""

        return self.build_root / "tests" / "gold_tests"

    @property
    def test_plugins(self) -> Path:
        """Return the directory containing test-only plugins."""

        return self.build_root / "tests" / "tools" / "plugins" / ".libs"

    def allocate_port(self, socket_type: int = socket.SOCK_STREAM) -> int:
        """Allocate a unique available port across all xdist workers."""

        common_sandbox = self.sandbox_root.parent
        common_sandbox.mkdir(parents=True, exist_ok=True)
        state_path = common_sandbox / ".port-counter"
        with state_path.open("a+") as state:
            fcntl.flock(state, fcntl.LOCK_EX)
            state.seek(0)
            content = state.read().strip()
            candidate = int(content) if content else 10000
            for _ in range(50000):
                candidate = 10000 if candidate >= 60000 else candidate + 1
                with socket.socket(socket.AF_INET, socket_type) as probe:
                    try:
                        probe.bind(("127.0.0.1", candidate))
                    except OSError:
                        continue
                state.seek(0)
                state.truncate()
                state.write(str(candidate))
                state.flush()
                fcntl.flock(state, fcntl.LOCK_UN)
                return candidate
            fcntl.flock(state, fcntl.LOCK_UN)
        raise RuntimeError("Could not allocate a test port")

    def item_sandbox(self, replay_path: Path, node_name: str) -> Path:
        """Return a deterministic, path-length-conscious sandbox for one item."""

        try:
            relative = replay_path.relative_to(self.repository_root / "tests" / "gold_tests")
        except ValueError:
            relative = replay_path.resolve()
        identity = f"{relative}:{node_name}"
        digest = hashlib.sha256(identity.encode()).hexdigest()[:10]
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", replay_path.stem)[:48]
        return self.sandbox_root / f"{stem}-{digest}"

    def prepare_sandbox(self, path: Path) -> None:
        """Create an empty item sandbox without allowing a broad deletion target."""

        resolved = path.resolve()
        if resolved.parent != self.sandbox_root or resolved == self.sandbox_root:
            raise RuntimeError(f"Refusing to clean unsafe sandbox path: {resolved}")
        if resolved.exists():
            shutil.rmtree(resolved)
        resolved.mkdir(parents=True)

    def requirement_failure(self, requirements: dict[str, Any]) -> str | None:
        """Return why a replay should be skipped, or None when it can run."""

        for feature in requirements.get("ats_features", []):
            if not self.features.get(feature):
                return f"ATS was built without {feature}"

        plugin_dir = Path(self.layout["PLUGINDIR"])
        for plugin in requirements.get("plugins", []):
            if not (plugin_dir / plugin).is_file():
                return f"ATS plugin is not installed: {plugin}"

        minimum_openssl = requirements.get("openssl")
        if minimum_openssl:
            output = subprocess.check_output(["openssl", "version"], text=True)
            if version_tuple(output) < version_tuple(str(minimum_openssl)):
                return f"OpenSSL {minimum_openssl} or newer is required"

        minimum_verifier = requirements.get("proxy_verifier")
        if minimum_verifier:
            output = subprocess.check_output([self.verifier_bin / "verifier-client", "--version"], text=True)
            if version_tuple(output) < version_tuple(str(minimum_verifier)):
                return f"Proxy Verifier {minimum_verifier} or newer is required"

        curl_requirements = requirements.get("curl_features", [])
        if curl_requirements:
            curl_output = subprocess.check_output(["curl", "--version"], text=True).lower()
            for feature in curl_requirements:
                if str(feature).lower() not in curl_output:
                    return f"curl does not provide {feature}"
        return None

    def resolve_artifact(self, test_directory: Path, value: str) -> Path:
        """Resolve a source or build artifact referenced by replay metadata."""

        variables = {
            "AtsTestPluginsDir": self.test_plugins,
            "ATS_TEST_PLUGINS_DIR": self.test_plugins,
            "AtsBuildGoldTestsDir": self.build_gold_tests,
            "RepoDir": self.repository_root,
        }
        expanded = re.sub(
            r"\{([A-Za-z_][A-Za-z0-9_]*)\}",
            lambda match: str(variables.get(match.group(1), match.group(0))),
            value,
        )
        candidate = Path(os.path.expandvars(expanded))
        if candidate.is_absolute():
            return candidate
        candidates = [test_directory / candidate]
        gold_source = self.repository_root / "tests" / "gold_tests"
        if test_directory.is_relative_to(gold_source):
            candidates.append(self.build_gold_tests / test_directory.relative_to(gold_source) / candidate)
        candidates.append(self.build_gold_tests / candidate)
        return next((path for path in candidates if path.exists()), candidates[-1])
