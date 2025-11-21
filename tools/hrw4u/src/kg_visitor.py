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

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.symbols import SymbolResolver
from hrw4u.states import SectionType
from hrw4u.visitor_base import BaseHRWVisitor
from hrw4u.common import SystemDefaults
from hrw4u.tables import (OPERATOR_MAP, CONDITION_MAP, FUNCTION_MAP, STATEMENT_FUNCTION_MAP, LSPPatternMatcher)
from hrw4u.types import SuffixGroup


@dataclass(slots=True, frozen=True)
class KGNode:
    id: str
    type: str
    properties: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {'id': self.id, 'type': self.type, 'properties': self.properties}


@dataclass(slots=True, frozen=True)
class KGEdge:
    source_id: str
    target_id: str
    relationship: str
    properties: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {
            'source_id': self.source_id,
            'target_id': self.target_id,
            'relationship': self.relationship,
            'properties': self.properties
        }


@dataclass(slots=True)
class KGData:
    nodes: list[KGNode]
    edges: list[KGEdge]

    def to_dict(self) -> dict[str, Any]:
        return {'nodes': [node.to_dict() for node in self.nodes], 'edges': [edge.to_dict() for edge in self.edges]}

    def merge(self, other: KGData) -> KGData:
        """Merge two KG data structures, avoiding duplicates."""
        existing_node_ids = {node.id for node in self.nodes}
        existing_edges = {(edge.source_id, edge.target_id, edge.relationship) for edge in self.edges}

        new_nodes = [node for node in other.nodes if node.id not in existing_node_ids]
        new_edges = [edge for edge in other.edges if (edge.source_id, edge.target_id, edge.relationship) not in existing_edges]

        return KGData(nodes=self.nodes + new_nodes, edges=self.edges + new_edges)


class KnowledgeGraphVisitor(hrw4uVisitor, BaseHRWVisitor):

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None) -> None:
        super().__init__(filename, debug, error_collector)

        self.symbol_resolver = SymbolResolver(debug)
        self.kg_data = KGData(nodes=[], edges=[])

        self.current_file_id: str | None = None
        self.current_section_id: str | None = None
        self.current_statement_id: str | None = None
        self._node_counter = 0

        self._create_semantic_knowledge_nodes()

    def _generate_id(self, node_type: str, identifier: str = None) -> str:
        if identifier:
            content_hash = hashlib.md5(f"{node_type}:{identifier}".encode()).hexdigest()[:8]
            return f"{node_type}_{content_hash}"
        else:
            self._node_counter += 1
            return f"{node_type}_{self._node_counter}"

    def _add_node(self, node_type: str, properties: dict[str, Any], identifier: str = None) -> str:
        node_id = self._generate_id(node_type, identifier)
        node = KGNode(id=node_id, type=node_type, properties=properties)
        self.kg_data.nodes.append(node)

        return node_id

    def _add_edge(self, source_id: str, target_id: str, relationship: str, properties: dict[str, Any] = None) -> None:
        if properties is None:
            properties = {}
        edge = KGEdge(source_id=source_id, target_id=target_id, relationship=relationship, properties=properties)
        self.kg_data.edges.append(edge)

    def _create_semantic_knowledge_nodes(self) -> None:
        """Create nodes representing the semantic knowledge from grammar and libraries."""

        grammar_rules = [
            "program", "section", "varSection", "statement", "conditional", "expression", "term", "factor", "comparison",
            "functionCall"
        ]

        for rule in grammar_rules:
            self._add_node(
                "GrammarRule", {
                    "name": rule,
                    "type": "parser_rule",
                    "description": f"ANTLR grammar rule for {rule}"
                }, f"grammar:{rule}")

        section_mappings = {
            "TXN_START": "TXN_START_HOOK",
            "PRE_REMAP": "READ_REQUEST_PRE_REMAP_HOOK",
            "REMAP": "REMAP_PSEUDO_HOOK",
            "READ_REQUEST": "READ_REQUEST_HDR_HOOK",
            "SEND_REQUEST": "SEND_REQUEST_HDR_HOOK",
            "READ_RESPONSE": "READ_RESPONSE_HDR_HOOK",
            "SEND_RESPONSE": "SEND_RESPONSE_HDR_HOOK",
            "TXN_CLOSE": "TXN_CLOSE_HOOK"
        }

        for hrw4u_section, ats_hook in section_mappings.items():
            self._add_node(
                "ATSHook", {
                    "name": ats_hook,
                    "hrw4u_section": hrw4u_section,
                    "description": f"Apache Traffic Server hook for {hrw4u_section}"
                }, f"ats_hook:{ats_hook}")

        for op_pattern, params in OPERATOR_MAP.items():
            validator = params.validate if params else None
            restricted_sections = params.sections if params else None
            command = params.target if params else None
            self._add_node(
                "SemanticOperator", {
                    "pattern": op_pattern,
                    "hrw_operator": str(command) if isinstance(command, str) else str(command),
                    "validates_uppercase": params.upper if params else False,
                    "has_validator": validator is not None,
                    "restricted_sections": [s.value for s in restricted_sections] if restricted_sections else None,
                    "description": f"Operator pattern {op_pattern} -> {command}"
                }, f"sem_op:{op_pattern}")

        for cond_pattern, params in CONDITION_MAP.items():
            validator = params.validate if params else None
            restricted = params.sections if params else None
            reverse_info = params.rev if params else None
            tag = params.target if params else None
            self._add_node(
                "SemanticCondition", {
                    "pattern": cond_pattern,
                    "hrw_condition": tag,
                    "validates_uppercase": params.upper if params else False,
                    "has_validator": validator is not None,
                    "restricted_sections": [s.value for s in restricted] if restricted else None,
                    "has_default_expression": params.prefix if params else False,
                    "reverse_mapping": reverse_info,
                    "description": f"Condition pattern {cond_pattern} -> {tag}"
                }, f"sem_cond:{cond_pattern}")

        for func_name, params in FUNCTION_MAP.items():
            self._add_node(
                "SemanticFunction", {
                    "name": func_name,
                    "hrw_condition": params.target,
                    "has_validator": params.validate is not None,
                    "type": "condition_function",
                    "description": f"Function {func_name} -> %{{{params.target}}}"
                }, f"sem_func:{func_name}")

        for func_name, params in STATEMENT_FUNCTION_MAP.items():
            self._add_node(
                "SemanticFunction", {
                    "name": func_name,
                    "hrw_operator": params.target,
                    "has_validator": params.validate is not None,
                    "type": "statement_function",
                    "description": f"Statement function {func_name} -> {params.target}"
                }, f"sem_stmt_func:{func_name}")

        for suffix_group in SuffixGroup:
            self._add_node(
                "SuffixGroup", {
                    "name": suffix_group.name,
                    "values": list(suffix_group.value),
                    "description": f"Valid suffixes for {suffix_group.name}"
                }, f"suffix_group:{suffix_group.name}")

    def get_kg_data(self) -> KGData:
        return self.kg_data

    def visitProgram(self, ctx) -> KGData:
        with self.debug_context("visitProgram"):
            file_path = Path(self.filename)
            try:
                relative_path = file_path.resolve().relative_to(Path.cwd())
                display_path = str(relative_path)
            except ValueError:
                display_path = self.filename

            self.current_file_id = self._add_node(
                "File", {
                    "path": display_path,
                    "name": file_path.name,
                    "extension": file_path.suffix,
                    "size_lines": len(ctx.getText().split('\n'))
                }, display_path)

            for item in ctx.programItem():
                if item.section():
                    self.visit(item.section())
                elif item.commentLine():
                    self._process_comment(item.commentLine())

            return self.kg_data

    def visitSection(self, ctx) -> None:
        with self.debug_context("visitSection"):
            if ctx.varSection():
                return self.visitVarSection(ctx.varSection())

            if ctx.name is None:
                return

            section_name = ctx.name.text

            try:
                section_type = SectionType(section_name)
                hook_name = section_type.hook_name
            except ValueError:
                hook_name = "UNKNOWN"

            self.current_section_id = self._add_node(
                "Section", {
                    "name": section_name,
                    "hook": hook_name,
                    "line_start": ctx.start.line if ctx.start else None,
                    "line_end": ctx.stop.line if ctx.stop else None,
                    "description": f"HRW4U section mapping to ATS hook {hook_name}",
                    "execution_phase": self._get_execution_phase(section_name)
                }, f"{self.filename}:{section_name}")

            if self.current_file_id:
                self._add_edge(self.current_file_id, self.current_section_id, "CONTAINS")

            # Link to ATS hook semantic node
            ats_hook_id = f"ats_hook:{hook_name}"
            if any(node.id == ats_hook_id for node in self.kg_data.nodes):
                self._add_edge(self.current_section_id, ats_hook_id, "MAPS_TO_ATS_HOOK")

            self.current_section = section_type
            for body_item in ctx.sectionBody():
                self.visit(body_item)

    def visitVarSection(self, ctx) -> None:
        with self.debug_context("visitVarSection"):
            vars_section_id = self._add_node(
                "VarSection", {
                    "line_start": ctx.start.line if ctx.start else None,
                    "line_end": ctx.stop.line if ctx.stop else None
                }, f"{self.filename}:VARS")

            if self.current_file_id:
                self._add_edge(self.current_file_id, vars_section_id, "CONTAINS")

            old_section_id = self.current_section_id
            self.current_section_id = vars_section_id

            self.visit(ctx.variables())

            self.current_section_id = old_section_id

    def visitVariableDecl(self, ctx) -> None:
        with self.debug_context("visitVariableDecl"):
            if ctx.name is None or ctx.typeName is None:
                return

            var_name = ctx.name.text
            var_type = ctx.typeName.text

            var_id = self._add_node(
                "Variable", {
                    "name": var_name,
                    "type": var_type,
                    "line": ctx.start.line if ctx.start else None
                }, f"{self.filename}:{var_name}")

            if self.current_section_id:
                self._add_edge(self.current_section_id, var_id, "DECLARES")

            try:
                self.symbol_resolver.declare_variable(var_name, var_type)
            except Exception:
                pass

    def visitStatement(self, ctx) -> None:
        with self.debug_context("visitStatement"):
            stmt_properties = {"line": ctx.start.line if ctx.start else None, "text": ctx.getText()}

            match ctx:
                case _ if ctx.BREAK():
                    stmt_id = self._add_node("Statement", {**stmt_properties, "type": "break"})

                case _ if ctx.functionCall():
                    stmt_id = self._process_function_call_statement(ctx.functionCall(), stmt_properties)

                case _ if ctx.EQUAL():
                    stmt_id = self._process_assignment_statement(ctx, stmt_properties)

                case _ if ctx.op:
                    stmt_id = self._add_node("Statement", {**stmt_properties, "type": "operator", "operator": ctx.op.text})

                case _:
                    stmt_id = self._add_node("Statement", {**stmt_properties, "type": "unknown"})

            if self.current_section_id and stmt_id:
                self._add_edge(self.current_section_id, stmt_id, "CONTAINS")

    def _process_function_call_statement(self, ctx, stmt_properties: dict) -> str:
        if ctx.funcName is None:
            return self._add_node("Statement", {**stmt_properties, "type": "function_call", "function": "unknown"})

        func_name = ctx.funcName.text
        args = []

        if ctx.argumentList():
            for arg_ctx in ctx.argumentList().value():
                args.append(arg_ctx.getText())

        stmt_id = self._add_node("Statement", {**stmt_properties, "type": "function_call", "function": func_name, "args": args})

        func_id = self._add_node("Function", {"name": func_name, "type": "builtin"}, func_name)
        self._add_edge(stmt_id, func_id, "CALLS")

        # Link to semantic function if exists
        sem_func_id = f"sem_stmt_func:{func_name}"
        if any(node.id == sem_func_id for node in self.kg_data.nodes):
            self._add_edge(func_id, sem_func_id, "HAS_SEMANTICS")

        return stmt_id

    def _process_assignment_statement(self, ctx, stmt_properties: dict) -> str:
        if ctx.lhs is None:
            return self._add_node("Statement", {**stmt_properties, "type": "assignment", "target": "unknown"})

        lhs = ctx.lhs.text
        rhs = ctx.value().getText() if ctx.value() else ""

        stmt_id = self._add_node("Statement", {**stmt_properties, "type": "assignment", "target": lhs, "value": rhs})

        if '.' in lhs:
            field_id = self._add_node("Field", {"name": lhs}, lhs)
            self._add_edge(stmt_id, field_id, "MODIFIES")

            # Add semantic information from pattern matching
            pattern_match = LSPPatternMatcher.match_any_pattern(lhs)
            if pattern_match:
                sem_cond_id = f"sem_cond:{pattern_match.pattern}"
                if any(node.id == sem_cond_id for node in self.kg_data.nodes):
                    self._add_edge(field_id, sem_cond_id, "MATCHES_PATTERN")

                for node in self.kg_data.nodes:
                    if node.id == field_id:
                        node.properties.update(
                            {
                                "pattern_type": pattern_match.context_type,
                                "matched_pattern": pattern_match.pattern,
                                "suffix": pattern_match.suffix
                            })
                        break

        elif symbol := self.symbol_resolver.symbol_for(lhs):
            var_id = self._add_node("Variable", {"name": lhs, "type": symbol.var_type.name}, lhs)
            self._add_edge(stmt_id, var_id, "ASSIGNS")

        return stmt_id

    def visitConditional(self, ctx) -> None:
        with self.debug_context("visitConditional"):
            cond_id = self._add_node(
                "Conditional", {
                    "line_start": ctx.start.line if ctx.start else None,
                    "line_end": ctx.stop.line if ctx.stop else None,
                    "has_elif": len(ctx.elifClause()) > 0,
                    "has_else": ctx.elseClause() is not None
                })

            if self.current_section_id:
                self._add_edge(self.current_section_id, cond_id, "CONTAINS")

            self._process_if_statement(ctx.ifStatement(), cond_id)

            for elif_ctx in ctx.elifClause():
                self._process_elif_statement(elif_ctx, cond_id)

            if ctx.elseClause():
                self._process_else_statement(ctx.elseClause(), cond_id)

    def _process_if_statement(self, ctx, parent_id: str) -> None:
        if_id = self._add_node("IfStatement", {"line": ctx.start.line if ctx.start else None})
        self._add_edge(parent_id, if_id, "HAS_IF")

        if ctx.condition():
            self._process_condition(ctx.condition(), if_id)

        if ctx.block():
            old_statement_id = self.current_statement_id
            self.current_statement_id = if_id
            self.visit(ctx.block())
            self.current_statement_id = old_statement_id

    def _process_elif_statement(self, ctx, parent_id: str) -> None:
        elif_id = self._add_node("ElifStatement", {"line": ctx.start.line if ctx.start else None})
        self._add_edge(parent_id, elif_id, "HAS_ELIF")

        if ctx.condition():
            self._process_condition(ctx.condition(), elif_id)

        if ctx.block():
            old_statement_id = self.current_statement_id
            self.current_statement_id = elif_id
            self.visit(ctx.block())
            self.current_statement_id = old_statement_id

    def _process_else_statement(self, ctx, parent_id: str) -> None:
        else_id = self._add_node("ElseStatement", {"line": ctx.start.line if ctx.start else None})
        self._add_edge(parent_id, else_id, "HAS_ELSE")

        if ctx.block():
            old_statement_id = self.current_statement_id
            self.current_statement_id = else_id
            self.visit(ctx.block())
            self.current_statement_id = old_statement_id

    def _process_condition(self, ctx, parent_id: str) -> None:
        condition_text = ctx.getText()
        cond_id = self._add_node("Condition", {"expression": condition_text, "line": ctx.start.line if ctx.start else None})
        self._add_edge(parent_id, cond_id, "HAS_CONDITION")

        self._extract_field_references(condition_text, cond_id)
        self._analyze_condition_semantics(condition_text, cond_id)

    def _extract_field_references(self, condition_text: str, condition_id: str) -> None:
        import re
        field_pattern = r'\b(inbound|outbound)\.[a-zA-Z0-9_.-]+\b'

        for match in re.finditer(field_pattern, condition_text):
            field_name = match.group(0)
            field_id = self._add_node("Field", {"name": field_name}, field_name)
            self._add_edge(condition_id, field_id, "REFERENCES")

            # Add semantic pattern information
            pattern_match = LSPPatternMatcher.match_any_pattern(field_name)
            if pattern_match:
                for node in self.kg_data.nodes:
                    if node.id == field_id:
                        node.properties.update(
                            {
                                "pattern_type": pattern_match.context_type,
                                "matched_pattern": pattern_match.pattern,
                                "suffix": pattern_match.suffix
                            })
                        break

                sem_cond_id = f"sem_cond:{pattern_match.pattern}"
                if any(node.id == sem_cond_id for node in self.kg_data.nodes):
                    self._add_edge(field_id, sem_cond_id, "MATCHES_PATTERN")

    def _analyze_condition_semantics(self, condition_text: str, condition_id: str) -> None:
        operators = []
        if '==' in condition_text:
            operators.append('EQUALS')
        if '!=' in condition_text:
            operators.append('NOT_EQUALS')
        if '~' in condition_text and '!~' not in condition_text:
            operators.append('REGEX_MATCH')
        if '!~' in condition_text:
            operators.append('REGEX_NOT_MATCH')
        if ' in ' in condition_text:
            operators.append('IN_SET')
        if '&&' in condition_text:
            operators.append('AND')
        if '||' in condition_text:
            operators.append('OR')

        for node in self.kg_data.nodes:
            if node.id == condition_id:
                node.properties['operators_used'] = operators
                node.properties['complexity'] = len(operators)
                break

        import re
        strings = re.findall(r'"([^"]*)"', condition_text)
        regexes = re.findall(r'/([^/]*?)/', condition_text)

        if strings or regexes:
            for node in self.kg_data.nodes:
                if node.id == condition_id:
                    if strings:
                        node.properties['string_literals'] = strings
                    if regexes:
                        node.properties['regex_patterns'] = regexes
                    break

    def _process_comment(self, ctx) -> None:
        if not ctx or not ctx.COMMENT():
            return

        comment_text = ctx.COMMENT().getText()
        comment_id = self._add_node("Comment", {"text": comment_text, "line": ctx.start.line if ctx.start else None})

        if self.current_section_id:
            self._add_edge(self.current_section_id, comment_id, "CONTAINS")
        elif self.current_file_id:
            self._add_edge(self.current_file_id, comment_id, "CONTAINS")

    def _get_execution_phase(self, section_name: str) -> str:
        phase_map = {
            "TXN_START": "transaction_start",
            "PRE_REMAP": "pre_remap",
            "REMAP": "remap",
            "READ_REQUEST": "post_remap",
            "SEND_REQUEST": "pre_origin",
            "READ_RESPONSE": "post_origin",
            "SEND_RESPONSE": "pre_client",
            "TXN_CLOSE": "transaction_end"
        }
        return phase_map.get(section_name, "unknown")

    def visitSectionBody(self, ctx) -> None:
        if ctx.statement():
            self.visit(ctx.statement())
        elif ctx.conditional():
            self.visit(ctx.conditional())
        elif ctx.commentLine():
            self._process_comment(ctx.commentLine())

    def visitBlock(self, ctx) -> None:
        for item in ctx.blockItem():
            if item.statement():
                self.visit(item.statement())
            elif item.commentLine():
                self._process_comment(item.commentLine())
