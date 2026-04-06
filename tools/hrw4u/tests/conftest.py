#
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
"""Shared fixtures and helpers for hrw4u tests."""
from __future__ import annotations

from pathlib import Path

import pytest

AUTEST_RULES_DIR = Path(
    __file__).resolve().parent.parent.parent.parent / "tests" / "gold_tests" / "pluginTest" / "header_rewrite" / "rules"


def collect_autest_pairs() -> list[pytest.param]:
    """Collect .conf/.hrw4u pairs from the autest rules directory."""
    if not AUTEST_RULES_DIR.is_dir():
        return []

    pairs = []
    for conf in sorted(AUTEST_RULES_DIR.glob("*.conf")):
        hrw4u = conf.with_suffix(".hrw4u")
        if hrw4u.exists():
            pairs.append(pytest.param(conf, hrw4u, id=conf.stem))

    return pairs
