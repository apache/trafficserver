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
from __future__ import annotations

import re
from pathlib import Path

import pytest
from antlr4 import InputStream, CommonTokenStream

from hrw4u.errors import ErrorCollector
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from u4wrh.hrw_visitor import HRWInverseVisitor
from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser

_RAW_PERCENT_RE = re.compile(r"%\{[^}]*\}")


def _hrw_to_hrw4u(hrw_text: str, filename: str) -> tuple[str, ErrorCollector]:
    lexer = u4wrhLexer(InputStream(hrw_text))
    stream = CommonTokenStream(lexer)
    parser = u4wrhParser(stream)
    tree = parser.program()
    collector = ErrorCollector()
    visitor = HRWInverseVisitor(filename=filename, error_collector=collector)
    hrw4u_text = "\n".join(visitor.visit(tree))
    return hrw4u_text, collector


def _hrw4u_to_hrw(hrw4u_text: str, filename: str) -> tuple[str, ErrorCollector]:
    lexer = hrw4uLexer(InputStream(hrw4u_text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()
    collector = ErrorCollector()
    visitor = HRW4UVisitor(filename=filename, error_collector=collector)
    hrw_text = "\n".join(visitor.visit(tree) or [])
    return hrw_text, collector


@pytest.mark.reverse
@pytest.mark.parametrize(
    "fixture_name",
    [
        "bare-client-url",
        "bare-client-cert-aprn",
        "bare-client-cert-san-dns",
    ],
)
def test_u4wrh_round_trip_no_raw_percent(fixture_name: str) -> None:
    """u4wrh must never emit raw %{...} in HRW4U output for legacy bare HRW tags.

    Verifies both HRW -> HRW4U produces valid, error-free HRW4U AND that
    HRW4U re-parses through hrw4u without errors (round-trip).
    """
    fixture_dir = Path("tests/data/conds")
    hrw_path = fixture_dir / f"{fixture_name}.output.txt"
    hrw_text = hrw_path.read_text()

    hrw4u_text, u4wrh_errors = _hrw_to_hrw4u(hrw_text, str(hrw_path))
    assert not u4wrh_errors.has_errors(), (f"u4wrh produced errors for {fixture_name}:\n{u4wrh_errors.get_error_summary()}")

    assert not _RAW_PERCENT_RE.search(hrw4u_text), (
        f"u4wrh emitted raw %{{...}} in HRW4U output for {fixture_name}:\n{hrw4u_text}")

    _, hrw4u_errors = _hrw4u_to_hrw(hrw4u_text, str(hrw_path))
    assert not hrw4u_errors.has_errors(), (
        f"hrw4u failed to re-parse u4wrh HRW4U output for {fixture_name}:\n"
        f"HRW4U:\n{hrw4u_text}\nErrors:\n{hrw4u_errors.get_error_summary()}")
