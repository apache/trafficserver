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
"""The AST must carry everything the emitter reads out of the source.

Hand-written cases only catch losses someone thought of; an empty `else { }`, a dropped
comment and a `TRUE`/`true` spelling all shipped because nobody did. So instead of naming
distinctions, run the whole corpus through the AST and back and require the compiled
config to be unchanged: whatever the AST drops, the config loses too.

        corpus .hrw4u  --parse-->  AST  --unparse-->  regenerated .hrw4u
              |                                              |
            emit                                           emit
              |                                              |
              v                                              v
           config  <----------- must match ------------>  config

The comparison is the compiled config, never the regenerated source text. An AST holds no
whitespace, indentation or blank lines, and the emitter reads none of them -- it even
indents a preserved comment by nesting depth rather than by the column it came from. So
demanding byte-identical source would fail on almost every input for reasons that change
no output, and satisfying it would mean turning the AST back into a CST.

A companion test asserts the corpus reaches every grammar rule, so a rule with no fixture
is reported rather than silently unguarded.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from antlr4 import ParserRuleContext

import ast_unparse
import utils
from hrw4u.ast_visitor import ASTVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor

CORPUS = Path("tests/data")

# Rejected before any sandbox check; named so a newly-broken input fails instead of dropping out.
DOES_NOT_COMPILE = frozenset({"sandbox/per-test-sandbox"})


def _case_id(input_file: Path) -> str:
    return f"{input_file.parent.name}/{input_file.name.removesuffix('.input.txt')}"


def _cases() -> list[pytest.param]:
    files = (f for f in sorted(CORPUS.glob("*/*.input.txt")) if ".fail." not in f.name)
    return [pytest.param(f, id=_case_id(f)) for f in files if _case_id(f) not in DOES_NOT_COMPILE]


def _compile(text: str, input_file: Path) -> list[str]:
    _, tree = utils.parse_input_text(text)
    visitor = HRW4UVisitor(filename=str(input_file), proc_search_paths=[input_file.parent / "procs"])
    return visitor.visit(tree)


@pytest.mark.parametrize("input_file", _cases())
def test_emitted_config_survives_a_round_trip_through_the_ast(input_file: Path) -> None:
    source = input_file.read_text()
    _, tree = utils.parse_input_text(source)
    regenerated = ast_unparse.unparse(ASTVisitor().visit(tree))

    expected = _compile(source, input_file)
    # An empty config would pass no matter what the AST drops.
    assert expected, f"{input_file} compiles to nothing; it cannot witness a round trip"
    assert _compile(regenerated, input_file) == expected, (
        f"{input_file}: the AST lost something the emitter reads.\n"
        f"--- regenerated hrw4u ---\n{regenerated}")


def test_the_corpus_reaches_every_grammar_rule() -> None:
    reached: set[str] = set()

    def walk(ctx) -> None:
        if isinstance(ctx, ParserRuleContext):
            reached.add(hrw4uParser.ruleNames[ctx.getRuleIndex()])
            for child in ctx.getChildren():
                walk(child)

    for param in _cases():
        _, tree = utils.parse_input_text(param.values[0].read_text())
        walk(tree)

    missing = set(hrw4uParser.ruleNames) - reached
    assert not missing, f"no corpus input exercises: {', '.join(sorted(missing))}"
