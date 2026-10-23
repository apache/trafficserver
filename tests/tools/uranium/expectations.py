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
"""Explicit expectations for captured Uranium process streams."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Never
import re

from .assertions import assert_matches_gold


@dataclass(frozen=True)
class StreamExpectation:
    """One assertion applied to a captured process stream."""

    kind: str
    value: str | Path
    explanation: str = ""
    reflags: int = 0


class StreamExpectations:
    """Explicit assertions and the captured path for one process stream."""

    def __init__(self, process_name: str, stream_name: str, path: Path, test_directory: Path) -> None:
        """Create an empty expectation collection for one stream.

        :param process_name: Name of the process that owns the stream.
        :param stream_name: Stream label, either ``stdout`` or ``stderr``.
        :param path: Path receiving captured process output.
        :param test_directory: Source directory for relative gold-file paths.
        """

        self._process_name = process_name
        self._stream_name = stream_name
        self._path = path
        self._test_directory = test_directory
        self._expectations: list[StreamExpectation] = []

    @property
    def expectations(self) -> tuple[StreamExpectation, ...]:
        """Return the registered expectations as an immutable tuple."""

        return tuple(self._expectations)

    @property
    def path(self) -> Path:
        """Return the path containing this stream's captured output."""

        return self._path

    def contains(self, expression: str, explanation: str, *, reflags: int = 0) -> None:
        """Require this stream to contain a regular expression.

        :param expression: Regular expression that must match the stream.
        :param explanation: Non-empty description of the expected behavior.
        :param reflags: Flags passed to :func:`re.search`.
        """

        self._require_explanation(explanation, "contains")
        self._expectations.append(StreamExpectation("contains", expression, explanation, reflags))

    def excludes(self, expression: str, explanation: str, *, reflags: int = 0) -> None:
        """Require this stream to exclude a regular expression.

        :param expression: Regular expression that must not match the stream.
        :param explanation: Non-empty description of the expected behavior.
        :param reflags: Flags passed to :func:`re.search`.
        """

        self._require_explanation(explanation, "excludes")
        self._expectations.append(StreamExpectation("excludes", expression, explanation, reflags))

    def matches_gold(self, path: str | Path, explanation: str = "") -> None:
        """Require this stream to match a wildcard-aware gold file.

        :param path: Gold-file path, relative to the test source directory.
        :param explanation: Optional description of the expected behavior.
        """

        self._expectations.append(StreamExpectation("gold", path, explanation))

    def reset(self) -> None:
        """Remove all registered expectations without changing stream identity."""

        self._expectations.clear()

    def validate(self, content: str) -> None:
        """Apply every registered expectation to captured stream content.

        :param content: Complete captured text for this stream.
        """

        normalized = content.replace("\r\n", "\n")
        for expectation in self._expectations:
            if expectation.kind == "contains":
                if re.search(str(expectation.value), normalized, expectation.reflags) is None:
                    self._fail("contain", expectation, normalized)
            elif expectation.kind == "excludes":
                if re.search(str(expectation.value), normalized, expectation.reflags) is not None:
                    self._fail("exclude", expectation, normalized)
            elif expectation.kind == "gold":
                path = Path(expectation.value)
                if not path.is_absolute():
                    path = self._test_directory / path
                try:
                    assert_matches_gold(normalized, path)
                except AssertionError as error:
                    prefix = f"{expectation.explanation}\n" if expectation.explanation else ""
                    raise AssertionError(
                        f"{prefix}Expected {self._process_name} {self._stream_name} to match {path}.\n{error}") from error
            else:
                raise ValueError(f"Unknown stream expectation: {expectation.kind}")

    def __add__(self, _value: object) -> Never:
        """Reject legacy augmented-assignment expectation registration.

        :param _value: Value supplied to an unsupported ``+=`` expression.
        """

        raise TypeError(
            f"{self._stream_name} does not support +=; use {self._stream_name}.contains(), "
            f"{self._stream_name}.excludes(), or {self._stream_name}.matches_gold()")

    def _fail(self, operation: str, expectation: StreamExpectation, content: str) -> Never:
        """Raise a diagnostic assertion for a regular-expression mismatch.

        :param operation: Human-readable expectation operation.
        :param expectation: Expectation that did not hold.
        :param content: Complete captured stream text.
        """

        raise AssertionError(
            f"{expectation.explanation}\nExpected {self._process_name} {self._stream_name} to {operation} "
            f"{str(expectation.value)!r}.\n--- {self._process_name} {self._stream_name} ---\n{content}")

    @staticmethod
    def _require_explanation(explanation: str, method: str) -> None:
        """Reject a regex expectation without a useful explanation.

        :param explanation: Explanation supplied by the test author.
        :param method: Expectation method being validated.
        """

        if not explanation.strip():
            raise ValueError(f"{method}() requires a non-empty explanation")
