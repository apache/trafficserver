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
"""Unit tests for pytest collection behavior."""

from dataclasses import dataclass, field
from typing import Any

import pytest

from tools.uranium.plugin import _mark_manual_tests


@dataclass
class FakeItem:
    """Record markers applied to one synthetic pytest item."""

    is_manual: bool
    manual_reason: str | None = None
    markers: list[Any] = field(default_factory=list)

    def get_closest_marker(self, name: str) -> pytest.Mark | None:
        """Return a marker only for manual synthetic items."""

        if name != "manual" or not self.is_manual:
            return None
        return pytest.mark.manual(reason=self.manual_reason).mark

    def add_marker(self, marker: Any, append: bool = True) -> None:
        """Record one marker added by the collection hook."""

        del append
        self.markers.append(marker)


def test_manual_tests_are_skipped_by_default() -> None:
    """Keep opt-in tests visible without executing them in normal runs."""

    manual = FakeItem(is_manual=True)
    regular = FakeItem(is_manual=False)

    _mark_manual_tests([manual, regular], enabled=False)

    assert [marker.mark.name for marker in manual.markers] == ["skip"]
    assert "--run-manual" in manual.markers[0].mark.kwargs["reason"]
    assert regular.markers == []


def test_manual_skip_preserves_the_marker_reason() -> None:
    """Explain why an opt-in scenario is excluded from normal runs."""

    manual = FakeItem(is_manual=True, manual_reason="requires root")

    _mark_manual_tests([manual], enabled=False)

    assert manual.markers[0].mark.kwargs["reason"] == "requires root; pass --run-manual to execute it"


def test_run_manual_enables_manual_tests() -> None:
    """Do not add a skip when the explicit opt-in flag is present."""

    manual = FakeItem(is_manual=True)

    _mark_manual_tests([manual], enabled=True)

    assert manual.markers == []
