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
r"""Every corpus input compiles the same way through both front ends.

Nothing in the product calls `compile_to_conf` yet, so this is what says the resolved tree
and its emitter are correct: the shipping CST emitter is the oracle and the whole corpus is
the input set -- successes, failures, and the sandbox fixtures under their own policies.
A failure is compared on its diagnostics alone, because a run that reported an error
writes no configuration.

                          [corpus .hrw4u]
                                 |
                                 | parse
                                 v
                           [parse tree]
                             /       \
                           /           \
              HRW4UVisitor               ASTVisitor   ---.
            (the CST oracle)                  |           |
                    |                       [AST]         |
                    |                         |           |
                    |                       Sema          |  compile_to_conf
                    |                         |           |
                    |                  [resolved tree]    |
                    |                         |           |
                    |                    ConfEmitter  ---'
                    |                         |
                    v                         v
          [config, diagnostics]     [config, diagnostics]
                    ^                         ^
                     \                       /
                      '---- must match -----'

Both rails are handed the same parse tree, error collector and sandbox policy, so the front
end is the only variable. Each input also runs without a collector, as `--stop-on-error` does,
where both must stop on the same first error. The diagnostics are in the comparison and not just a "did it
fail" bool, because a duplicated warning, a dropped note or an error on the `if` line instead
of the `else` line leaves the config untouched and would pass a weaker assertion.
"""

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

import pytest
import utils
import yaml

from hrw4u.common import compile_to_conf
from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError
from hrw4u.sandbox import SandboxConfig
from hrw4u.visitor import HRW4UVisitor


class CstBug(Enum):
    """A known way the CST emitter gets an input wrong, and so where Sema must differ from it."""
    # Raises out of visit() despite an error collector, so its caller gets an exception
    # instead of a diagnostic. Sema collects the same error and carries on, which is the
    # behaviour the collector was asked for.
    RAISES = auto()


CST_BUGS = {"procedures/wrong-arity": CstBug.RAISES}

COLLECT_MODES = [pytest.param(True, id="collected"), pytest.param(False, id="stop-on-error")]


def _case_id(input_file: Path) -> str:
    return f"{input_file.parent.name}/{input_file.name.removesuffix('.input.txt').removesuffix('.fail')}"


def _policy_for(input_file: Path) -> SandboxConfig | None:
    if sandbox_file := utils.sandbox_file_for(input_file):
        return SandboxConfig.load(str(sandbox_file))
    return None


def _corpus() -> list[pytest.param]:
    return [pytest.param(f, id=_case_id(f)) for f in sorted(Path("tests/data").glob("*/*.input.txt"))]


@dataclass(frozen=True)
class Outcome:
    lines: list[str] | None
    errors: tuple
    warnings: tuple
    raised: tuple | None


def _run(front_end: str, tree, filename: str, text: str, *, collect: bool, **kwargs) -> Outcome:
    collector = None
    if collect:
        collector = ErrorCollector()
    try:
        if front_end == "cst":
            lines = HRW4UVisitor(filename=filename, error_collector=collector, **kwargs).visit(tree)
        else:
            lines = compile_to_conf(tree, filename, text, error_collector=collector, **kwargs)
        raised = None
    except BaseException as e:  # SystemExit is a BaseException and must not pass silently
        lines, raised = None, _error(e)
    errors, warnings = (), ()
    if collector:
        errors, warnings = _diagnostics(collector)
    return Outcome(lines=lines, errors=errors, warnings=warnings, raised=raised)


def _compile(front_end: str, tree, input_file: Path, text: str, *, collect: bool) -> Outcome:
    return _run(
        front_end,
        tree,
        str(input_file),
        text,
        collect=collect,
        sandbox=_policy_for(input_file),
        proc_search_paths=[input_file.parent / "procs"])


@pytest.mark.parametrize("collect", COLLECT_MODES)
@pytest.mark.parametrize("input_file", _corpus())
def test_front_ends_agree(input_file: Path, collect: bool) -> None:
    text = input_file.read_text()
    _, tree = utils.parse_input_text(text)

    old = _compile("cst", tree, input_file, text, collect=collect)
    new = _compile("sema", tree, input_file, text, collect=collect)

    _assert_outcomes(old, new, collect=collect, cst_bug=CST_BUGS.get(_case_id(input_file)))


def _assert_outcomes(old: Outcome, new: Outcome, *, collect: bool, cst_bug: CstBug | None) -> None:
    match cst_bug:
        case CstBug.RAISES if collect:
            _assert_collects_what_cst_raises(old, new)
        case _:
            _assert_agree(old, new, collect=collect)


def _assert_agree(old: Outcome, new: Outcome, *, collect: bool) -> None:
    if collect:
        # Two identical crashes would otherwise compare equal.
        assert old.raised is None, f"the CST emitter raised despite a collector: {old.raised}"
    assert (new.raised, new.errors, new.warnings) == (old.raised, old.errors, old.warnings)
    # A run that reported an error writes no configuration, so what either front end left in
    # its lines is not a product to agree on.
    if old.raised is None and not old.errors:
        assert _conf(new.lines) == _conf(old.lines)


def _assert_collects_what_cst_raises(old: Outcome, new: Outcome) -> None:
    assert old.raised is not None, "no longer raises; drop its CstBug"
    assert new.raised is None
    assert new.errors[:1] == (old.raised,)


def _conf(lines: list[str] | None) -> str:
    # Stripped as the golden comparisons in utils.py strip it: a trailing blank line is not a difference.
    return "\n".join(lines or []).strip()


def _diagnostics(collector: ErrorCollector) -> tuple[tuple, tuple]:
    """Every diagnostic's position, message and notes, in order: (errors, warnings).

    Not `has_errors()`: a bool agrees whenever both sides merely fail, which hides a wrong
    message, a duplicated warning and a diagnostic pointing at the wrong line -- none of
    which change the config, and all of which a user reads. Not the rendered summary
    either, because that carries the echoed source line, and visitor.py hands its procedure
    errors an empty one (_load_proc_file passes ""), so comparing it would assert that the
    new path reproduces a defect rather than that the two agree.
    """
    return (
        tuple(_error(e) for e in collector.errors),
        tuple((w.filename, w.line, w.column, w.message) for w in collector.warnings),
    )


def _error(e: BaseException) -> tuple:
    if isinstance(e, Hrw4uSyntaxError):
        return (e.filename, e.line, e.column, e.message, tuple(getattr(e, "__notes__", None) or ()))
    return (type(e).__name__, str(e))


def test_the_corpus_is_not_empty() -> None:
    # A glob that silently matches nothing would make every assertion above vacuous.
    corpus = _corpus()
    assert len(corpus) > 130
    assert any(".fail." in p.values[0].name for p in corpus)
    assert any(_policy_for(p.values[0]) is not None for p in corpus)


@dataclass(frozen=True)
class Case:
    """A construct the corpus has no fixture for."""
    source: str
    deny: dict | None = None  # the sandbox `deny:` block
    warn: dict | None = None  # the sandbox `warn:` block
    comments: bool = True
    procedure: str | None = None  # written to <tmp>/procs/syn/p.hrw4u, reachable as `use syn::p`
    cst_bug: CstBug | None = None


SYNTHETIC = [
    pytest.param(Case('REMAP {\n    # only\n}\n', comments=False), id="comment-only-section-comments-off"),
    pytest.param(Case('REMAP {\n    # only\n}\n'), id="comment-only-section-comments-on"),
    pytest.param(Case('REMAP {\n    outbound.resp.X-A = "1";\n}\n'), id="section-whose-only-statement-fails"),
    pytest.param(Case('REMAP {\n    if inbound.status > 399 {\n        set-debug();\n    } else {\n    }\n}\n'), id="empty-else"),
    pytest.param(
        Case(
            'REMAP {\n    if inbound.req.Path in ["a","b"] with EXT,NOCASE {\n        set-debug();\n    }\n}\n',
            deny={"modifiers": ["EXT"]}),
        id="first-of-two-modifiers-denied"),
    pytest.param(
        Case(
            'REMAP {\n    if inbound.req.Path in ["a","b"] with NOCASE,EXT {\n        set-debug();\n    }\n}\n',
            deny={"modifiers": ["EXT"]}),
        id="second-of-two-modifiers-denied"),
    pytest.param(Case('VARS {\n    a: bool;\n    b: bool;\n}\nREMAP {\n    a = b;\n}\n'), id="variable-to-variable-assignment"),
    pytest.param(
        Case('REMAP {\n    if inbound.req.Host == "x" with BOGUS {\n        set-debug();\n    }\n}\n'), id="unknown-modifier"),
    pytest.param(
        Case('REMAP {\n    if inbound.req.Host == "x" with PRE,SUF {\n        set-debug();\n    }\n}\n'),
        id="conflicting-substring-modifiers"),
    pytest.param(Case('REMAP {\n    if access("") {\n        set-debug();\n    }\n}\n'), id="function-with-an-empty-argument"),
    pytest.param(Case('REMAP {\n    inbound.req.X-A = 007;\n}\n'), id="number-with-a-leading-zero"),
    pytest.param(Case('REMAP {\n    counter($nope);\n}\n'), id="param-ref-outside-a-procedure"),
    pytest.param(
        Case(
            'REMAP {\n    if inbound.req.Host == "a" && inbound.req.Host == "b" {\n        set-debug();\n    }\n}\n',
            warn={"conditions": ["inbound.req."]}),
        id="one-warned-condition-used-twice"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    syn::p("v");\n}\n', procedure='procedure syn::p($a) {\n    inbound.req.X-A = $zzz;\n}\n'),
        id="unbound-param-in-a-procedure-body"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n    syn::p("w");\n}\n',
            procedure='procedure syn::p($a) {\n    inbound.req.X-A = $zzz;\n}\n'),
        id="failed-procedure-called-twice"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    outbound.resp.X-B = "1";\n    inbound.req.X-A = $zzz;\n}\n'),
        id="unbound-param-after-a-failed-statement"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    inbound.req.X-A = $zzz;\n    inbound.req.X-B = $yyy;\n}\n'),
        id="two-unbound-params"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    if inbound.req.X == $zzz {\n        no-op();\n    }\n'
            '    outbound.resp.X-B = "1";\n}\n'),
        id="unbound-param-in-a-comparison-before-a-failed-statement"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n'
            '    if (inbound.req.Host == $a || inbound.req.X-B == "x-$a") && !(inbound.req.X-C == $a) {\n'
            '        no-op();\n    }\n}\n'),
        id="param-in-a-compound-condition-in-a-procedure"),
    pytest.param(
        Case('VARS {\n    f: bool;\n}\nprocedure syn::p() {\n    f = true;\n}\nREMAP {\n    syn::p();\n}\n'),
        id="procedure-declared-after-vars"),
    pytest.param(
        Case(
            'VARS {\n    f: bool;\n}\nuse syn::p\nREMAP {\n    syn::p();\n}\n',
            procedure='procedure syn::p() {\n    no-op();\n}\n'),
        id="use-after-vars"),
    pytest.param(
        Case('VARS {\n    n: int8;\n}\nREMAP {\n    inbound.req.X-A = "{n with NORM}";\n}\n'),
        id="variable-interpolation-with-a-value-modifier"),
    pytest.param(
        Case('VARS {\n    n: int8;\n}\nREMAP {\n    inbound.req.X-A = "{n with NOCASE}";\n}\n'),
        id="variable-interpolation-with-a-condition-modifier"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    if inbound.status > 399 {\n        set-debug();\n    }\n'
            '    else {\n        set-debug();\n    }\n}\n',
            deny={"language": ["else"]}),
        id="denied-else-in-a-procedure"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    if inbound.req.Host == "x"\n        with NOCASE {\n'
            '        set-debug();\n    }\n}\n',
            deny={"modifiers": ["NOCASE"]}),
        id="denied-modifier-in-a-procedure"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    syn::p("v");\n}\n', procedure='procedure syn::p($a) {\n    inbound.req.$h = "1";\n}\n'),
        id="syntax-error-in-a-procedure-file"),
    pytest.param(Case('REMAP {\n    inbound.req.X-A = $nope;\n}\n'), id="param-ref-outside-a-procedure-in-an-assignment"),
    pytest.param(
        Case('REMAP {\n    if inbound.req.X-A == $nope {\n        set-debug();\n    }\n}\n'),
        id="param-ref-outside-a-procedure-in-a-comparison"),
    pytest.param(
        Case('REMAP {\n    if access("/tmp/{x}") {\n        set-debug();\n    }\n}\n'),
        id="braces-in-a-condition-function-argument"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p();\n}\n',
            procedure='procedure syn::p($a = "{inbound.req.Host}") {\n    inbound.req.X-A = $a;\n}\n'),
        id="string-default-is-not-interpolated"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    syn::p("5");\n}\n', procedure='procedure syn::p($a) {\n    counter($a);\n}\n'),
        id="param-ref-as-an-operator-argument-in-a-procedure"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    # hi\n    syn::p();\n}\n', procedure='procedure syn::p() {\n    no-op();\n}\n'),
        id="leading-comment-before-a-procedure-call"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    syn::p();\n}\n', procedure='procedure syn::p() {\n    # c\n}\n'),
        id="comment-only-procedure"),
    pytest.param(Case('use syn::p\nREMAP {\n    syn::p();\n}\n', procedure='procedure syn::p() {\n}\n'), id="empty-procedure"),
    pytest.param(
        Case('use syn::p\nREMAP {\n    syn::p();\n}\n', procedure='procedure syn::p() {\n    # c\n    no-op();\n}\n'),
        id="procedure-opening-with-a-comment"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p();\n}\n', procedure='procedure syn::p() {\n    # c\n    no-op();\n}\n',
            comments=False),
        id="procedure-opening-with-a-comment-comments-off"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    if inbound.status > 1 {\n        no-op();\n    }\n    syn::p();\n}\n',
            procedure='procedure syn::p() {\n    if inbound.status > 1 {\n        no-op();\n    }\n}\n'),
        id="procedure-after-an-if"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::q();\n}\n',
            procedure='procedure syn::p() {\n    # c\n    no-op();\n}\nprocedure syn::q() {\n    syn::p();\n    no-op();\n}\n'),
        id="nested-procedure-first"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p($x);\n}\n',
            procedure='procedure syn::p($a) {\n    inbound.req.X-A = $a;\n}\n',
            cst_bug=CstBug.RAISES),
        id="param-ref-passed-to-a-procedure-outside-one"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::p("v");\n}\n',
            procedure='procedure syn::p($a) {\n    inbound.req.X-A = "{bogus} $a";\n}\n'),
        id="interpolation-note-in-a-procedure"),
    pytest.param(
        Case(
            'use syn::p\nREMAP {\n    syn::q("v");\n}\n',
            procedure='procedure syn::p($b) {\n    inbound.req.X-A = $b;\n}\n'
            'procedure syn::q($a) {\n    syn::p("{bogus} $a");\n}\n'),
        id="interpolation-note-in-a-nested-call-argument"),
]


@pytest.mark.parametrize("collect", COLLECT_MODES)
@pytest.mark.parametrize("case", SYNTHETIC)
def test_front_ends_agree_on_uncovered_constructs(case: Case, collect: bool, tmp_path: Path) -> None:
    kwargs: dict = {"preserve_comments": case.comments}
    if policy := {k: v for k, v in (("deny", case.deny), ("warn", case.warn)) if v}:
        config = tmp_path / "sandbox.yaml"
        config.write_text(yaml.safe_dump({"sandbox": policy}))
        kwargs["sandbox"] = SandboxConfig.load(str(config))
    if case.procedure is not None:
        namespace = tmp_path / "procs" / "syn"
        namespace.mkdir(parents=True)
        (namespace / "p.hrw4u").write_text(case.procedure)
        kwargs["proc_search_paths"] = [tmp_path / "procs"]

    _, tree = utils.parse_input_text(case.source)
    old = _run("cst", tree, "<synthetic>", case.source, collect=collect, **kwargs)
    new = _run("sema", tree, "<synthetic>", case.source, collect=collect, **kwargs)

    _assert_outcomes(old, new, collect=collect, cst_bug=case.cst_bug)
