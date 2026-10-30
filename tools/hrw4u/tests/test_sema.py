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
"""The resolved tree, the lookups that feed it, and the pass that builds it.

`sema_nodes` is what a resolved program lowers to, `symbols.lookup_*` resolves a source
name to the table entry behind it, and `sema` walks an AST to produce the tree. These are
unit expectations, written out literally; whether the tree renders the same `.conf` as the
shipping emitter is `test_front_end_parity.py`.
"""

import dataclasses
import typing

import yaml

import pytest
from utils import parse_input_text

from hrw4u.ast_nodes import Assignment, ProcedureDecl, Span
from hrw4u.ast_visitor import ASTVisitor
from hrw4u.common import HeaderOperations
from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError, SourceMap, SymbolResolutionError
from hrw4u.procedures import ProcRegistry
from hrw4u.sandbox import SandboxConfig
from hrw4u.sema import Sema
from hrw4u.sema_nodes import (
    And, BoolCond, BreakStmt, BuiltinRef, CallOp, CmpOp, CommentStmt, Cond, FuncCond, Group, IpRangeLit, ModifiedRef, Not,
    OpVariant, Or, Program, RawArg, RegexLit, ResolvedSection, SetLit, SetOp, SetVar, Stmt, StrTemplate, Test, Truth, Unresolved,
    VarsBlock, VarRef)
from hrw4u.states import SectionType
from hrw4u.symbols import SymbolResolver
from hrw4u.tables import FUNCTION_MAP
from hrw4u.types import MapParams, Symbol, VarType, VarScope


class TestValueText:
    """How a resolved reference renders as header_rewrite `%{...}` value text."""

    def test_exact_entry_renders_its_target(self):
        ref = BuiltinRef(key="inbound.method", entry=MapParams(target="%{METHOD}"), arg=None)
        assert ref.as_value_text() == "%{METHOD}"

    def test_prefix_entry_wraps_tag_and_arg(self):
        ref = BuiltinRef(key="inbound.req.", entry=MapParams(target="CLIENT-HEADER"), arg="X-Foo")
        assert ref.as_value_text() == "%{CLIENT-HEADER:X-Foo}"

    def test_variable_ref_renders_its_slot(self):
        ref = VarRef(symbol=Symbol(VarType.BOOL, 3, VarScope.TXN))
        assert ref.as_value_text() == "%{STATE-FLAG:3}"

    def test_bare_target_needs_wrap(self):
        ref = BuiltinRef(key="txn-count", entry=MapParams(target="TXN-COUNT"), arg=None, wrap=True)
        assert ref.as_value_text() == "%{TXN-COUNT}"

    def test_wrap_is_off_by_default_so_prewrapped_targets_survive(self):
        ref = BuiltinRef(key="now", entry=MapParams(target="%{NOW}"), arg=None)
        assert ref.as_value_text() == "%{NOW}"

    def test_value_modifiers_go_inside_the_braces(self):
        ref = BuiltinRef(key="inbound.url.", entry=MapParams(target="CLIENT-URL"), arg="PATH")
        assert ModifiedRef(ref=ref, mods=("NORM",)).as_value_text() == "%{CLIENT-URL:PATH [NORM]}"


class TestEnums:
    """A member interpolates as its text."""

    def test_cmp_op_interpolates_as_its_operator(self):
        # A plain (str, Enum) renders as "CmpOp.EQ" under str()/format() on 3.11+.
        assert f"{CmpOp.EQ}" == "=="
        assert str(CmpOp.NOT_IN) == "!in"

    def test_op_variant_interpolates_as_its_name(self):
        assert f"{OpVariant.REMOVE}" == "REMOVE"


class TestLookupCondition:
    """The three ways a condition name resolves, and the error when it resolves to nothing."""

    def test_exact_entry(self):
        got = SymbolResolver().lookup_condition("inbound.method", SectionType.REMAP)
        assert got.key == "inbound.method"
        assert got.entry.target == "%{METHOD}"
        assert got.arg is None
        assert got.existence is False

    def test_prefix_entry_normalizes_and_flags_existence(self):
        got = SymbolResolver().lookup_condition("inbound.req.X-Foo", SectionType.REMAP)
        assert got.key == "inbound.req."
        assert got.entry.target == "CLIENT-HEADER"
        assert got.arg == "X-Foo"  # the inbound.req. entry sets no upper=, so no case change
        assert got.existence is True

    def test_prefix_entry_uppercases_when_the_entry_says_so(self):
        got = SymbolResolver().lookup_condition("inbound.url.host", SectionType.REMAP)
        assert got.arg == "HOST"  # CLIENT-URL entry has upper=True

    def test_declared_variable(self):
        r = SymbolResolver()
        r.declare_variable("flag", "bool")
        got = r.lookup_condition("flag", SectionType.REMAP)
        assert got.symbol is not None
        assert got.key is None

    def test_unknown_symbol_raises(self):
        with pytest.raises(SymbolResolutionError, match="Unknown condition symbol"):
            SymbolResolver().lookup_condition("nope.nothing", SectionType.REMAP)


class TestLookupOperator:
    """Prefix, exact and variable assignment each give a different OpLookup; `+=` on an
    entry without the flag raises instead."""

    def test_prefix_operator_splits_qualifier(self):
        got = SymbolResolver().lookup_operator("inbound.req.X-Foo", '"bar"', SectionType.REMAP, add=False)
        assert got.key == "inbound.req."
        assert got.qualifier == "X-Foo"

    def test_exact_operator_has_no_qualifier(self):
        got = SymbolResolver().lookup_operator("inbound.status", "404", SectionType.REMAP, add=False)
        assert got.key == "inbound.status"
        assert got.qualifier is None

    def test_add_rejected_when_entry_lacks_the_flag(self):
        with pytest.raises(SymbolResolutionError, match="not supported"):
            SymbolResolver().lookup_operator("inbound.status", "404", SectionType.REMAP, add=True)

    def test_variable_assignment(self):
        r = SymbolResolver()
        r.declare_variable("flag", "bool")
        got = r.lookup_operator("flag", "true", SectionType.REMAP, add=False)
        assert got.symbol is not None


class TestAddPathCaseNormalization:
    """`=` upper-cases the qualifier where the entry asks for it; `+=` leaves it alone.

    This pins what `resolve_add_assignment` does today, not a rule worth keeping:
    it simply has no `params.upper` branch. No shipped entry sets
    `add=` and `upper=` together, so the divergence is unreachable -- the first one that
    does should fix both paths and rewrite this test, not satisfy it.
    """

    @staticmethod
    def _resolver_with_both_flags() -> SymbolResolver:
        """No shipped OPERATOR_MAP entry sets add= and upper= together, so inject one."""
        r = SymbolResolver()
        r._operator_map = {"test.hdr.": MapParams(target=HeaderOperations.OPERATIONS, add=True, upper=True)}
        return r

    def test_add_path_keeps_the_qualifier_case(self):
        got = self._resolver_with_both_flags().lookup_operator("test.hdr.X-MiXeD", '"v"', SectionType.REMAP, add=True)
        assert got.qualifier == "X-MiXeD"

    def test_non_add_path_uppercases_the_qualifier(self):
        got = self._resolver_with_both_flags().lookup_operator("test.hdr.X-MiXeD", '"v"', SectionType.REMAP, add=False)
        assert got.qualifier == "X-MIXED"


class TestLookupFunctions:
    """Function and statement-function entries, including the in-place argument normalization."""

    def test_function(self):
        key, entry = SymbolResolver().lookup_function("access", ['"/tmp/bar"'])
        assert (key, entry.target) == ("access", "ACCESS")

    def test_statement_function_normalizes_args_in_place(self):
        args = ['yes']
        key, entry = SymbolResolver().lookup_statement_function("skip-remap", args, SectionType.REMAP)
        assert key == "skip-remap"
        assert args == ['YES']


class TestFunctionRef:
    """A function call resolves to a BuiltinRef over a FUNCTION_MAP entry, which is what lets it
    sit on the left of a Test the way the grammar's `comparable : ident | functionCall` allows."""

    CASES = [
        ("access", ["foo"]),
        ("access", ['"/tmp/bar"']),
        ("cache", []),
        ("cidr", ["8", "64"]),
        ("internal", []),
        ("random", ["100"]),
        ("ssn-txn-count", []),
        ("txn-count", []),
    ]

    def test_cases_cover_every_function_entry(self):
        assert {name for name, _ in self.CASES} == set(FUNCTION_MAP)

    @pytest.mark.parametrize("name,args", CASES)
    def test_matches_resolve_function_with_quotes_kept(self, name, args):
        # resolve_function has two callers and they disagree about quoting: _substitute_strings
        # (string interpolation) keeps them, visitFunctionCall (condition position) strips
        # them. This pins the keep-them form, which is what BuiltinRef models; the strip
        # variant has no equivalent here and moves with its consumer.
        key, entry = SymbolResolver().lookup_function(name, list(args))
        ref = BuiltinRef.from_function(key, entry, args)
        assert ref.as_value_text() == SymbolResolver().resolve_function(name, list(args))

    def test_sits_on_the_left_of_a_test(self):
        key, entry = SymbolResolver().lookup_function("txn-count", [])
        cond = Test(
            ref=BuiltinRef.from_function(key, entry, []),
            op=CmpOp.GT,
            rhs=RawArg(text="10"),
            modifiers=(),
            span=Span("cond.hrw4u", 1, 0))
        assert cond.ref.as_value_text() == "%{TXN-COUNT}"


class TestNodeShape:
    """Fields that exist so a diagnostic or an emitter can rely on them."""

    @pytest.mark.parametrize("union", [Cond, Stmt])
    def test_every_node_carries_a_span(self, union):
        # A sema error has to report a location, so no member may be the one without one.
        for member in typing.get_args(union):
            assert "span" in {f.name for f in dataclasses.fields(member)}, member.__name__

    def test_hook_is_derived_from_the_section(self):
        span = Span("s.hrw4u", 1, 0)
        # VARS is excluded: a variable section lowers to VarsBlock, never to ResolvedSection.
        for section in (s for s in SectionType if s is not SectionType.VARS):
            assert ResolvedSection(section=section, body=(), span=span).hook == section.hook_name


def _sema(source: str, **kwargs) -> Program:
    _, tree = parse_input_text(source)
    ast = ASTVisitor(filename="<test>").visit(tree)
    s = Sema(**kwargs)
    s.source_map.add("<test>", source)
    return s.analyze(ast)


class TestSemaTopLevel:

    def test_section_resolves_its_hook(self):
        prog = _sema('REMAP {\n    set-debug();\n}')
        assert isinstance(prog.items[0], ResolvedSection)
        assert prog.items[0].section == SectionType.REMAP
        assert prog.items[0].hook == "REMAP_PSEUDO_HOOK"

    def test_top_level_comment_survives(self):
        prog = _sema('# note\nREMAP {\n    set-debug();\n}')
        assert prog.items[0] == CommentStmt(text="# note", span=prog.items[0].span)

    def test_comments_dropped_when_disabled(self):
        prog = _sema('# note\nREMAP {\n    set-debug();\n}', preserve_comments=False)
        assert all(not isinstance(i, CommentStmt) for i in prog.items)

    def test_vars_declare_slots_and_emit_only_comments(self):
        prog = _sema('VARS {\n    # a counter\n    flag: bool;\n    n: int8;\n}\nREMAP {\n    set-debug();\n}')
        assert isinstance(prog.items[0], VarsBlock)
        assert [c.text for c in prog.items[0].comments] == ["# a counter"]
        assert isinstance(prog.items[1], ResolvedSection)

    def test_vars_without_comments_is_still_an_item(self):
        prog = _sema('VARS {\n    flag: bool;\n}\nREMAP {\n    set-debug();\n}')
        assert isinstance(prog.items[0], VarsBlock)
        assert prog.items[0].comments == ()

    def test_session_vars_use_the_session_scope(self):
        prog = _sema('SESSION_VARS {\n    flag: bool;\n}\nREMAP {\n    flag = true;\n}')
        assert prog.items[-1].body[0].symbol.scope == VarScope.SESSION

    def test_invalid_section_name_is_reported(self):
        ec = ErrorCollector()
        _sema('BOGUS {\n    set-debug();\n}', error_collector=ec)
        assert "Invalid section name: 'BOGUS'" in ec.get_error_summary()

    def test_vars_after_a_section_is_reported(self):
        ec = ErrorCollector()
        _sema('REMAP {\n    set-debug();\n}\nVARS {\n    flag: bool;\n}', error_collector=ec)
        assert "Variable section must be first in a section" in ec.get_error_summary()

    def test_session_vars_after_a_section_is_reported(self):
        ec = ErrorCollector()
        _sema('REMAP {\n    set-debug();\n}\nSESSION_VARS {\n    flag: bool;\n}', error_collector=ec)
        assert "Session variable section must be first in a section" in ec.get_error_summary()


def _stmts(source: str, **kwargs):
    # The last item is the section under test: a leading VARS/SESSION_VARS block
    # (see test_variable_assignment) is its own preceding item, not the section itself.
    return _sema(source, **kwargs).items[-1].body


class TestSemaStatements:

    def test_header_set(self):
        s = _stmts('REMAP {\n    inbound.req.X-Foo = "bar";\n}')[0]
        assert isinstance(s, SetOp)
        assert (s.key, s.qualifier, s.variant) == ("inbound.req.", "X-Foo", OpVariant.SET)
        assert s.value == StrTemplate(parts=("bar",))

    def test_empty_value_selects_remove(self):
        s = _stmts('REMAP {\n    inbound.req.X-Foo = "";\n}')[0]
        assert s.variant == OpVariant.REMOVE

    def test_plus_equals_selects_add(self):
        s = _stmts('REMAP {\n    inbound.req.X-Foo += "bar";\n}')[0]
        assert s.variant == OpVariant.ADD

    def test_exact_operator_keeps_a_raw_value(self):
        s = _stmts('REMAP {\n    inbound.status = 404;\n}')[0]
        assert (s.key, s.qualifier, s.value) == ("inbound.status", None, RawArg(text="404"))

    def test_variable_assignment(self):
        s = _stmts('VARS {\n    flag: bool;\n}\nREMAP {\n    flag = true;\n}')[0]
        assert isinstance(s, SetVar)
        assert s.symbol.slot == 0

    def test_statement_function(self):
        s = _stmts('REMAP {\n    set-debug();\n}')[0]
        assert isinstance(s, CallOp)
        assert (s.key, s.args) == ("set-debug", ())

    def test_break(self):
        assert isinstance(_stmts('REMAP {\n    if inbound.status > 399 {\n        break;\n    }\n}')[0].body[0], BreakStmt)

    def test_interpolated_string_becomes_a_template(self):
        s = _stmts('REMAP {\n    inbound.req.X-M = "m={inbound.method}";\n}')[0]
        assert s.value.parts[0] == "m="
        assert isinstance(s.value.parts[1], BuiltinRef)

    def test_interpolation_modifiers_wrap_the_reference(self):
        s = _stmts('REMAP {\n    inbound.req.X-P = "{inbound.url.path with norm}";\n}')[0]
        part = s.value.parts[0]
        assert isinstance(part, ModifiedRef)
        assert (part.ref.key, part.ref.arg, part.mods) == ("inbound.url.", "PATH", ("NORM",))

    def test_interpolation_without_modifiers_stays_a_bare_reference(self):
        s = _stmts('REMAP {\n    inbound.req.X-P = "{inbound.url.path}";\n}')[0]
        assert isinstance(s.value.parts[0], BuiltinRef)

    def test_comment_between_statements(self):
        body = _stmts('REMAP {\n    set-debug();\n    # why\n    no-op();\n}')
        assert [type(b).__name__ for b in body] == ["CallOp", "CommentStmt", "CallOp"]

    def test_unknown_statement_function_is_reported(self):
        ec = ErrorCollector()
        _sema('REMAP {\n    nope();\n}', error_collector=ec)
        assert "Unknown statement function: 'nope'" in ec.get_error_summary()


def _cond(source: str, **kwargs):
    return _stmts(source, **kwargs)[0].cond


class TestSemaConditions:

    def test_comparison(self):
        c = _cond('REMAP {\n    if inbound.status > 399 {\n        set-debug();\n    }\n}')
        assert isinstance(c, Test)
        assert c.op == CmpOp.GT
        assert c.ref.key == "inbound.status"
        assert c.rhs == RawArg(text="399")

    def test_not_equal_keeps_the_op(self):
        c = _cond('REMAP {\n    if inbound.method != "GET" {\n        set-debug();\n    }\n}')
        assert c.op == CmpOp.NE

    def test_regex_match(self):
        c = _cond('REMAP {\n    if inbound.req.Host ~ /example/ {\n        set-debug();\n    }\n}')
        assert c.op == CmpOp.MATCH
        assert c.rhs == RegexLit(text="/example/")

    def test_set_membership(self):
        c = _cond('REMAP {\n    if inbound.method in ["GET","HEAD"] {\n        set-debug();\n    }\n}')
        assert c.op == CmpOp.IN
        assert c.rhs == SetLit(text='"GET","HEAD"')

    def test_and_or_nesting(self):
        c = _cond('REMAP {\n    if inbound.status > 399 && inbound.status < 500 {\n        set-debug();\n    }\n}')
        assert isinstance(c, And)

    def test_or(self):
        c = _cond('REMAP {\n    if inbound.status > 399 || inbound.status < 200 {\n        set-debug();\n    }\n}')
        assert isinstance(c, Or)

    def test_group_and_not(self):
        c = _cond('REMAP {\n    if !(inbound.status > 399) {\n        set-debug();\n    }\n}')
        assert isinstance(c, Not)
        assert isinstance(c.inner, Group)

    def test_bare_prefix_ident_is_an_existence_test(self):
        c = _cond('REMAP {\n    if inbound.req.X-Foo {\n        set-debug();\n    }\n}')
        assert isinstance(c, Truth)
        assert c.existence is True

    def test_bare_variable_is_not_an_existence_test(self):
        c = _cond('VARS {\n    flag: bool;\n}\nREMAP {\n    if flag {\n        set-debug();\n    }\n}')
        assert isinstance(c, Truth)
        assert c.existence is False

    def test_function_condition(self):
        c = _cond('REMAP {\n    if access("/tmp/bar") {\n        set-debug();\n    }\n}')
        assert isinstance(c, FuncCond)
        assert c.key == "access"

    def test_true_literal(self):
        c = _cond('REMAP {\n    if true {\n        set-debug();\n    }\n}')
        assert c == BoolCond(value=True, span=c.span)

    def test_modifiers_are_carried(self):
        c = _cond('REMAP {\n    if inbound.req.Host == "x" with NOCASE {\n        set-debug();\n    }\n}')
        assert c.modifiers == ("NOCASE",)

    def test_undeclared_bare_variable_is_reported(self):
        ec = ErrorCollector()
        _sema('REMAP {\n    if nosuchvar {\n        set-debug();\n    }\n}', error_collector=ec)
        assert "Undefined variable: 'nosuchvar'" in ec.get_error_summary()

    def test_bare_builtin_is_not_an_undeclared_variable(self):
        ec = ErrorCollector()
        c = _cond('REMAP {\n    if now {\n        set-debug();\n    }\n}', error_collector=ec)
        assert isinstance(c, Truth)
        assert c.ref.key == "now"
        assert ec.get_error_summary() == "No errors found."

    def test_failed_function_lhs_reports_exactly_one_error(self):
        ec = ErrorCollector()
        _sema('REMAP {\n    if nosuchfunc() == "x" {\n        set-debug();\n    }\n}', error_collector=ec)
        assert ec.get_error_summary().count("error:") == 1

    def test_zero_arg_function_lhs_is_a_builtin_ref(self):
        ec = ErrorCollector()
        c = _cond('READ_RESPONSE {\n    if cache() == "hit-stale" {\n        set-debug();\n    }\n}', error_collector=ec)
        assert ec.get_error_summary() == "No errors found."
        assert isinstance(c, Test)
        assert c.ref.key == "cache"

    def test_iprange_membership(self):
        c = _cond('SEND_REQUEST {\n    if inbound.ip in {192.168.0.0/16, 10.0.0.0/8} {\n        set-debug();\n    }\n}')
        assert c.op == CmpOp.IN
        assert isinstance(c.rhs, IpRangeLit)


class TestProcRegistry:

    def test_use_registers_declarations(self, tmp_path):
        # 'use' specs and procedure names are QUALIFIED_IDENT in the grammar (>=1 '::'),
        # so both segments here are load-bearing, not arbitrary test data.
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "Hdr.hrw4u").write_text('procedure Base::Hdr() {\n    inbound.req.X-P = "1";\n}\n')
        source = 'use Base::Hdr\nREMAP {\n    set-debug();\n}'
        _, tree = parse_input_text(source)
        ast_tree = ASTVisitor(filename="<test>").visit(tree)
        s = Sema(proc_search_paths=[procs])
        s.source_map.add("<test>", source)
        s.analyze(ast_tree)
        sig = s.procs.get("Base::Hdr")
        assert sig is not None
        assert isinstance(sig.body[0], Assignment)

    def test_unqualified_procedure_name_is_rejected(self):
        # An unqualified procedure name can't come from the parser (PROCEDURE requires a
        # QUALIFIED_IDENT), so this exercises ProcRegistry.register() directly.
        reg = ProcRegistry(source_map=SourceMap())
        decl = ProcedureDecl(name="bare", params=(), body=(), span=Span(file="<test>", line=1, column=0))
        with pytest.raises(Hrw4uSyntaxError, match="must be qualified"):
            reg.register(decl)

    def test_duplicate_declaration_is_rejected(self):
        ec = ErrorCollector()
        _sema('procedure A::x() {\n    set-debug();\n}\nprocedure A::x() {\n    no-op();\n}\nREMAP {\n}', error_collector=ec)
        assert "already declared" in ec.get_error_summary()

    def test_declaration_after_a_section_is_rejected(self):
        ec = ErrorCollector()
        _sema('REMAP {\n}\nprocedure A::x() {\n    set-debug();\n}', error_collector=ec)
        assert "must appear before any section blocks" in ec.get_error_summary()

    def test_optional_before_required_parameter_is_rejected(self):
        ec = ErrorCollector()
        _sema('procedure A::x($a = "1", $b) {\n    set-debug();\n}\nREMAP {\n}', error_collector=ec)
        assert "must not follow an optional parameter" in ec.get_error_summary()

    def test_namespace_mismatch_message_keeps_the_full_use_spec(self, tmp_path):
        # _load_proc_file keeps the untruncated 'use' spec in the second half of this
        # message while the first half uses the truncated namespace; register()'s single-arg
        # form collapsed both halves to the truncated namespace. Pin the full wording here
        # since no golden checks more than the 'does not match namespace' substring.
        procs = tmp_path / "procs"
        (procs / "test").mkdir(parents=True)
        (procs / "test" / "wrong-namespace.hrw4u").write_text('procedure wrong::namespace() {\n    inbound.req.X-Bad = "1";\n}\n')
        ec = ErrorCollector()
        _sema('use test::wrong-namespace\nREMAP {\n    set-debug();\n}', proc_search_paths=[procs], error_collector=ec)
        summary = ec.get_error_summary()
        assert "does not match namespace 'test'" in summary
        assert "(expected from 'use test::wrong-namespace')" in summary


class TestProcedureInlining:

    def test_body_is_spliced(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "two.hrw4u").write_text('procedure Base::two() {\n    set-debug();\n    no-op();\n}\n')
        body = _sema('use Base::two\nREMAP {\n    Base::two();\n}', proc_search_paths=[procs]).items[0].body
        assert [type(b).__name__ for b in body] == ["Inlined"]
        assert [type(b).__name__ for b in body[0].body] == ["CallOp", "CallOp"]
        assert body[0].opens_with_statement

    def test_a_leading_comment_is_not_the_opening_statement(self, tmp_path):
        # Even with comments dropped: visitor.py counts the comment's position either way.
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "one.hrw4u").write_text('procedure Base::one() {\n    # c\n    no-op();\n}\n')
        for comments in (True, False):
            program = _sema('use Base::one\nREMAP {\n    Base::one();\n}', proc_search_paths=[procs], preserve_comments=comments)
            assert not program.items[0].body[0].opens_with_statement

    def test_parameter_substitution(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "hdr.hrw4u").write_text('procedure Base::hdr($v) {\n    inbound.req.X-P = "$v";\n}\n')
        s = _sema('use Base::hdr\nREMAP {\n    Base::hdr("yes");\n}', proc_search_paths=[procs]).items[0].body[0].body[0]
        assert s.value == StrTemplate(parts=("yes",))

    def test_default_parameter(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "hdr.hrw4u").write_text('procedure Base::hdr($v = "def") {\n    inbound.req.X-P = "$v";\n}\n')
        s = _sema('use Base::hdr\nREMAP {\n    Base::hdr();\n}', proc_search_paths=[procs]).items[0].body[0].body[0]
        assert s.value == StrTemplate(parts=("def",))

    def test_numeric_default_parameter_stays_unquoted(self, tmp_path):
        """A ParamRef binding must not be wrapped in LiteralStringValue, which would quote a
        numeric default ('300' instead of 300)."""
        procs = tmp_path / "procs"
        (procs / "test").mkdir(parents=True)
        (procs / "test" /
         "set-cache.hrw4u").write_text('procedure test::set-cache($ttl=300) {\n    inbound.req.X-Cache-TTL = $ttl;\n}\n')
        s = _sema('use test::set-cache\nREMAP {\n    test::set-cache();\n}', proc_search_paths=[procs]).items[0].body[0].body[0]
        assert s.value == RawArg(text="300")

    def test_bare_parameter_substitutes_unquoted(self, tmp_path):
        """_get_value_text hands a bare `$p` its binding text, so a string argument loses
        its quotes on the way to the operator -- `set-header X-Tag hello`, not `"hello"`."""
        procs = tmp_path / "procs"
        (procs / "test").mkdir(parents=True)
        (procs / "test" / "tag.hrw4u").write_text('procedure test::tag($name) {\n    inbound.req.X-Tag = $name;\n}\n')
        s = _sema('use test::tag\nREMAP {\n    test::tag("hello");\n}', proc_search_paths=[procs]).items[0].body[0].body[0]
        assert s.value == RawArg(text="hello")

    def test_arity_mismatch_is_reported(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "hdr.hrw4u").write_text('procedure Base::hdr($v) {\n    inbound.req.X-P = "$v";\n}\n')
        ec = ErrorCollector()
        _sema('use Base::hdr\nREMAP {\n    Base::hdr();\n}', proc_search_paths=[procs], error_collector=ec)
        assert "expected 1 arg(s), got 0" in ec.get_error_summary()

    def test_call_cycle_is_reported(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "a.hrw4u").write_text('procedure Base::a() {\n    Base::a();\n}\n')
        ec = ErrorCollector()
        _sema('use Base::a\nREMAP {\n    Base::a();\n}', proc_search_paths=[procs], error_collector=ec)
        assert "circular procedure call" in ec.get_error_summary()

    def test_mutual_call_cycle_names_the_whole_chain(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" /
         "a.hrw4u").write_text('procedure Base::p() {\n    Base::q();\n}\nprocedure Base::q() {\n    Base::p();\n}\n')
        ec = ErrorCollector()
        _sema('use Base::a\nREMAP {\n    Base::p();\n}', proc_search_paths=[procs], error_collector=ec)
        assert [e.message for e in ec.errors] == ["circular procedure call: Base::p -> Base::q -> Base::p"]

    def test_inlined_span_names_the_procedure_file(self, tmp_path):
        procs = tmp_path / "procs"
        (procs / "Base").mkdir(parents=True)
        (procs / "Base" / "hdr.hrw4u").write_text('procedure Base::hdr() {\n    inbound.req.X-P = "1";\n}\n')
        inlined = _sema('use Base::hdr\nREMAP {\n    Base::hdr();\n}', proc_search_paths=[procs]).items[0].body[0]
        assert not inlined.span.file.endswith("hdr.hrw4u")
        assert inlined.body[0].span.file.endswith("hdr.hrw4u")


class TestDenialDoesNotFailOpen:
    """A sandbox denial must not leave behind a guard that matches everything."""

    @staticmethod
    def _analyze(tmp_path, source: str, policy: dict):
        config = tmp_path / "sandbox.yaml"
        config.write_text(yaml.safe_dump({"sandbox": {"deny": policy}}))
        collector = ErrorCollector()
        _, tree = parse_input_text(source)
        s = Sema(error_collector=collector, sandbox=SandboxConfig.load(str(config)))
        s.source_map.add("<test>", source)
        return s.analyze(ASTVisitor(filename="<test>").visit(tree)), collector

    def test_denied_modifier_keeps_the_comparison(self, tmp_path):
        prog, collector = self._analyze(
            tmp_path, 'REMAP {\n    if inbound.req.X-Foo == "bar" with NOCASE {\n        inbound.req.X-R = "y";\n    }\n}\n',
            {"modifiers": ["NOCASE"]})
        cond = prog.items[0].body[0].cond
        assert collector.has_errors()
        assert isinstance(cond, Test), f"the guard became {type(cond).__name__}"
        assert cond.modifiers == ()

    def test_unresolvable_condition_is_not_a_true_guard(self, tmp_path):
        prog, collector = self._analyze(tmp_path, 'REMAP {\n    if nope.nothing == "x" {\n        break;\n    }\n}\n', {})
        assert collector.has_errors()
        assert isinstance(prog.items[0].body[0].cond, Unresolved)

    def test_denied_statement_costs_only_itself(self, tmp_path):
        prog, _ = self._analyze(tmp_path, 'REMAP {\n    inbound.req.X-A = "1";\n    break;\n}\n', {"language": ["break"]})
        assert [type(b).__name__ for b in prog.items[0].body] == ["SetOp"]
