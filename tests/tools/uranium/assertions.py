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
"""Shared assertions for direct and procedural Uranium tests."""

from __future__ import annotations

from pathlib import Path
import difflib
import re

try:
    from cdifflib import CSequenceMatcher
except ImportError:
    USING_ACCELERATED_DIFF = False
else:
    difflib.SequenceMatcher = CSequenceMatcher
    USING_ACCELERATED_DIFF = True


def assert_matches_gold(actual: str | Path, expected: Path) -> None:
    """Assert that output matches a Uranium wildcard gold file.

    :param actual: Captured output or its file path.
    :param expected: Gold-file path containing literal text and wildcards.
    """

    if isinstance(actual, Path):
        if not actual.is_file():
            raise AssertionError(f"Actual output file does not exist: {actual}")
        actual_text = actual.read_text(errors="replace")
        actual_name = str(actual)
    else:
        actual_text = actual
        actual_name = "actual"
    actual_text = actual_text.replace("\r\n", "\n")
    if not expected.is_file():
        raise AssertionError(f"Gold file does not exist: {expected}")
    expected_text = expected.read_text(errors="replace").replace("\r\n", "\n")
    expected_text = expected_text.replace("\n``\n", "``")
    pattern = "\\A" + ".*?".join(re.escape(part) for part in re.split(r"(?:\{\}|``)", expected_text)) + "\\Z"
    if re.match(pattern, actual_text, re.DOTALL) is None:
        difference = "".join(
            difflib.unified_diff(expected_text.splitlines(True), actual_text.splitlines(True), str(expected), actual_name))
        raise AssertionError(f"Output did not match gold file:\n{difference}")
