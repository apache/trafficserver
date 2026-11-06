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
"""
Procedure path resolution for hrw4u.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from hrw4u.errors import Hrw4uSyntaxError, SourceMap

if TYPE_CHECKING:
    import hrw4u.ast_nodes as ast


def resolve_use_path(spec: str, search_paths: list[Path]) -> Path | None:
    """Resolve 'foo::bar::Baz' to <search_path>/foo/bar/Baz.hrw4u."""
    rel = Path(*spec.split('::')).with_suffix('.hrw4u')
    return next((base / rel for base in search_paths if (base / rel).is_file()), None)


@dataclass(frozen=True, slots=True)
class ProcEntry:
    name: str
    params: tuple[ast.ProcParam, ...]
    body: tuple[ast.BodyNode, ...]
    source_file: str


@dataclass
class ProcRegistry:
    """Procedure declarations, keyed by qualified name."""

    source_map: SourceMap
    _sigs: dict[str, ProcEntry] = field(default_factory=dict, init=False)
    _loaded: set[str] = field(default_factory=set, init=False)

    def get(self, name: str) -> ProcEntry | None:
        return self._sigs.get(name)

    def register(self, decl: ast.ProcedureDecl, expected_ns: str | None = None, use_spec: str | None = None) -> None:
        span = decl.span
        if '::' not in decl.name:
            raise self.source_map.error_at(span, f"procedure name '{decl.name}' must be qualified (e.g. 'ns::name')")
        if expected_ns and not decl.name.startswith(expected_ns):
            namespace = expected_ns.removesuffix('::')
            raise self.source_map.error_at(
                span, f"procedure '{decl.name}' does not match namespace '{namespace}' (expected from 'use {use_spec}')"
            )
        if existing := self._sigs.get(decl.name):
            raise self.source_map.error_at(span, f"procedure '{decl.name}' already declared in {existing.source_file}")

        seen_default = False
        for param in decl.params:
            if param.default is None and seen_default:
                raise self.source_map.error_at(
                    span, f"procedure '{decl.name}': required parameter '${param.name}' must not follow an optional parameter"
                )
            if param.default is not None:
                seen_default = True

        self._sigs[decl.name] = ProcEntry(name=decl.name, params=decl.params, body=decl.body, source_file=span.file)

    def load(self, spec: str, span: ast.Span, search_paths: list[Path], load_stack: list[str] | None = None) -> None:
        from antlr4 import CommonTokenStream, InputStream
        from antlr4.error.ErrorStrategy import BailErrorStrategy

        import hrw4u.ast_nodes as ast
        from hrw4u.ast_visitor import ASTVisitor
        from hrw4u.errors import ThrowingErrorListener
        from hrw4u.hrw4uLexer import hrw4uLexer
        from hrw4u.hrw4uParser import hrw4uParser

        if not search_paths:
            raise self.source_map.error_at(span, "use directive requires --procedures-path to be set")

        path = resolve_use_path(spec, search_paths)
        if path is None:
            raise self.source_map.error_at(span, f"use '{spec}': file not found in procedures path")

        abs_path = str(path.resolve())
        stack = load_stack or []
        if abs_path in self._loaded:
            return
        if abs_path in stack:
            raise Hrw4uSyntaxError(str(path), 1, 0, f"circular use dependency: {' -> '.join([*stack, abs_path])}", "")

        text = path.read_text(encoding='utf-8')
        self.source_map.add(str(path), text)
        # Not create_parse_tree, which exits the process: a syntax error must raise into the caller's trap.
        listener = ThrowingErrorListener(filename=str(path))
        lexer = hrw4uLexer(InputStream(text))
        lexer.removeErrorListeners()
        lexer.addErrorListener(listener)
        parser = hrw4uParser(CommonTokenStream(lexer))
        parser.removeErrorListeners()
        parser.addErrorListener(listener)
        parser.errorHandler = BailErrorStrategy()
        file_ast = ASTVisitor(filename=str(path)).visit(parser.program())

        # 'use Apple::Common' requires every declaration in the file to start 'Apple::'.
        expected_ns = None
        if '::' in spec:
            expected_ns = spec[: spec.rindex('::') + 2]
        found_proc = False

        for node in file_ast.body:
            if isinstance(node, ast.UseDirective):
                self.load(node.spec, node.span, search_paths, [*stack, abs_path])
                found_proc = True
            elif isinstance(node, ast.ProcedureDecl):
                self.register(node, expected_ns, spec)
                found_proc = True

        if not found_proc:
            raise Hrw4uSyntaxError(str(path), 1, 0, f"no 'procedure' declarations found in {path.name}", "")

        # Only once the file processed cleanly: a failed file must not satisfy a later `use`.
        self._loaded.add(abs_path)
