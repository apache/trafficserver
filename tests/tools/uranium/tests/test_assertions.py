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
"""Unit tests for assertions shared by both Uranium test styles."""

from pathlib import Path
import difflib

import pytest

from tools.uranium.assertions import USING_ACCELERATED_DIFF, assert_matches_gold


def test_gold_file_wildcards_match_variable_text(tmp_path: Path) -> None:
    """Preserve the established `` and {} wildcard tokens in gold files.

    :param tmp_path: Temporary directory for comparison inputs.
    """

    expected = tmp_path / "expected.gold"
    expected.write_text("port=`` id={} done\n")
    assert_matches_gold("port=43127 id=abc-123 done\n", expected)


def test_standalone_gold_wildcard_absorbs_its_line_breaks(tmp_path: Path) -> None:
    """Apply standalone wildcard semantics consistently to both test styles.

    :param tmp_path: Temporary directory for comparison inputs.
    """

    expected = tmp_path / "expected.gold"
    expected.write_text("before\n``\nafter\n")
    assert_matches_gold("before arbitrary text after\n", expected)


def test_gold_file_difference_fails(tmp_path: Path) -> None:
    """Report a mismatch when fixed gold-file text changes.

    :param tmp_path: Temporary directory for comparison inputs.
    """

    expected = tmp_path / "expected.gold"
    expected.write_text("expected\n")
    with pytest.raises(AssertionError, match="did not match"):
        assert_matches_gold("actual\n", expected)


def test_missing_gold_file_has_an_assertion_message(tmp_path: Path) -> None:
    """Report a missing expectation without leaking a file-read traceback.

    :param tmp_path: Temporary directory for the absent gold file.
    """

    with pytest.raises(AssertionError, match="Gold file does not exist"):
        assert_matches_gold("actual\n", tmp_path / "missing.gold")


def test_missing_actual_file_has_an_assertion_message(tmp_path: Path) -> None:
    """Report missing captured output without treating it as empty output.

    :param tmp_path: Temporary directory for the absent output file.
    """

    expected = tmp_path / "expected.gold"
    expected.write_text("expected\n")
    with pytest.raises(AssertionError, match="Actual output file does not exist"):
        assert_matches_gold(tmp_path / "missing.out", expected)


def test_cdifflib_is_used_when_available() -> None:
    """Select the C sequence matcher whenever the optional module imports."""

    if not USING_ACCELERATED_DIFF:
        pytest.skip("cdifflib is unavailable")
    assert difflib.SequenceMatcher.__name__ == "CSequenceMatcher"
