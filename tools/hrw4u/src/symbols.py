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

from typing import Callable
from hrw4u.validation import Validator
from hrw4u.errors import SymbolResolutionError
import hrw4u.types as types
from hrw4u.states import SectionType
from hrw4u.common import SystemDefaults
from hrw4u.symbols_base import SymbolResolverBase
from hrw4u.suggestions import SuggestionEngine


class SymbolResolver(SymbolResolverBase):

    def __init__(self, debug: bool = SystemDefaults.DEFAULT_DEBUG) -> None:
        super().__init__(debug)
        self._symbols: dict[str, types.Symbol] = {}
        self._var_counter = {vt: 0 for vt in types.VarType}
        self._suggestion_engine = SuggestionEngine()

    def symbol_for(self, name: str) -> types.Symbol | None:
        return self._symbols.get(name)

    def get_statement_spec(self, name: str) -> tuple[str, Callable[[str], None] | None]:
        # Use cached lookup from base class
        if params := self._lookup_statement_function_cached(name):
            return params.target, params.validate
        raise SymbolResolutionError(name, "Unknown operator or invalid standalone use")

    def declare_variable(self, name: str, type_name: str) -> str:
        try:
            var_type = types.VarType.from_str(type_name)
        except ValueError as e:
            error = SymbolResolutionError(name, f"Invalid type '{type_name}'")
            error.add_note(f"Available types: {', '.join([vt.name for vt in types.VarType])}")
            raise error

        if self._var_counter[var_type] >= var_type.limit:
            error = SymbolResolutionError(name, f"Too many '{type_name}' variables (max {var_type.limit})")
            error.add_note(f"Current count: {self._var_counter[var_type]}")
            raise error

        symbol = types.Symbol(var_type, self._var_counter[var_type])
        self._var_counter[var_type] += 1
        self._symbols[name] = symbol
        return symbol.as_cond()

    def resolve_assignment(self, name: str, value: str, section: SectionType | None = None) -> str:
        with self.debug_context("resolve_assignment", name, value, section):
            for op_key, params in self._operator_map.items():
                if op_key.endswith("."):
                    if name.startswith(op_key):
                        self.validate_section_access(name, section, params.sections if params else None)
                        qualifier = name[len(op_key):]
                        if params and params.upper:
                            qualifier = qualifier.upper()
                        if params and params.validate:
                            params.validate(qualifier)

                        # Add boolean value validation for http.cntl assignments
                        if op_key == "http.cntl.":
                            types.SuffixGroup.BOOL_FIELDS.validate(value)

                        commands = params.target if params else None
                        if isinstance(commands, (list, tuple)):
                            return f"{commands[0 if value == '\"\"' else 1]} {qualifier}" + ("" if value == '""' else f" {value}")
                        return f"{commands} {qualifier} {value}"

                elif name == op_key:
                    # Exact match - validate and return
                    self.validate_section_access(name, section, params.sections if params else None)
                    if params and params.validate:
                        params.validate(value)
                    return f"{params.target if params else None} {value}"

            if resolved_lhs := self.symbol_for(name):
                if resolved_rhs := self.symbol_for(value):
                    if resolved_rhs.var_type != resolved_lhs.var_type:
                        raise SymbolResolutionError(value, f"Type mismatch: {resolved_lhs.var_type} vs {resolved_rhs.var_type}")
                    return resolved_lhs.as_operator(resolved_rhs.as_cond())

                Validator.validate_assignment(resolved_lhs.var_type, value, name)
                return resolved_lhs.as_operator(value)

            error = SymbolResolutionError(name, "Unknown assignment symbol")
            suggestions = self._suggestion_engine.get_suggestions(name, 'assignment', section)
            if suggestions:
                error.add_symbol_suggestion(suggestions)
            raise error

    def resolve_add_assignment(self, name: str, value: str, section: SectionType | None = None) -> str:
        """Resolve += assignment, if it is supported for the given operator."""
        with self.debug_context("resolve_add_assignment", name, value, section):
            for op_key, params in self._operator_map.items():
                if op_key.endswith(".") and name.startswith(op_key) and params and params.add:
                    self.validate_section_access(name, section, params.sections)
                    qualifier = name[len(op_key):]
                    if params.validate:
                        params.validate(qualifier)

                    from hrw4u.common import HeaderOperations
                    return f"{HeaderOperations.ADD_OPERATION} {qualifier} {value}"

            # += not allowed if no matching operator with 'add' flag found
            error = SymbolResolutionError(name, "+= operator is not supported for this assignment")
            error.add_note("Only operators with 'add' flag support +=")
            raise error

    def resolve_condition(self, name: str, section: SectionType | None = None) -> tuple[str, bool]:
        with self.debug_context("resolve_condition", name, section):
            if symbol := self.symbol_for(name):
                return symbol.as_cond(), False

            if params := self._lookup_condition_cached(name):
                tag = params.target if params else None
                restricted = params.sections if params else None
                self.validate_section_access(name, section, restricted)
                # For exact matches, default_expr is determined by whether it's a prefix pattern
                return tag, False

            # Check prefix matches using base class utility
            prefix_matches = self.find_prefix_matches(name, self._condition_map)
            for prefix, params in prefix_matches:
                tag = params.target if params else None
                validator = params.validate if params else None
                restricted = params.sections if params else None

                self.validate_section_access(name, section, restricted)
                suffix = name[len(prefix):]
                suffix_norm = suffix.upper() if (params and params.upper) else suffix
                if validator:
                    validator(suffix_norm)
                resolved = f"%{{{tag}:{suffix_norm}}}"
                # For prefix matches, default_expr is True (indicated by prefix flag)
                return resolved, (params.prefix if params else False)

            error = SymbolResolutionError(name, "Unknown condition symbol")
            declared_vars = list(self._symbols.keys())
            suggestions = self._suggestion_engine.get_suggestions(name, 'condition', section, declared_vars)
            if suggestions:
                error.add_symbol_suggestion(suggestions)
            raise error

    def resolve_function(self, func_name: str, args: list[str], strip_quotes: bool = False) -> str:
        with self.debug_context("resolve_function", func_name, args):
            if params := self._lookup_function_cached(func_name):
                tag = params.target
                validator = params.validate
                if validator:
                    validator(args)

                if strip_quotes:
                    cleaned_args = [arg[1:-1] if arg.startswith('"') and arg.endswith('"') else arg for arg in args]
                else:
                    cleaned_args = args

                return f"%{{{tag}:{','.join(cleaned_args)}}}" if cleaned_args else f"%{{{tag}}}"

            error = SymbolResolutionError(func_name, f"Unknown function: '{func_name}'")
            error.add_note(f"Arguments provided: {len(args)}")
            suggestions = self._suggestion_engine.get_suggestions(func_name, 'function')
            if suggestions:
                error.add_symbol_suggestion(suggestions)
            raise error

    def resolve_statement_func(self, func_name: str, args: list[str]) -> str:
        with self.debug_context("resolve_statement_func", func_name, args):
            if params := self._lookup_statement_function_cached(func_name):
                command = params.target
                validator = params.validate
                if validator:
                    validator(args)

                result = command if not args else f"{command} {' '.join(args)}"
                # TODO: Move this special case to states.py module
                if func_name == "keep_query":
                    result += " [I]"
                return result

            error = SymbolResolutionError(func_name, f"Unknown statement function: '{func_name}'")
            error.add_note(f"Arguments provided: {len(args)}")
            suggestions = self._suggestion_engine.get_suggestions(func_name, 'statement_function')
            if suggestions:
                error.add_symbol_suggestion(suggestions)
            raise error

    def get_variable_suggestions(self, name: str, section: SectionType | None = None) -> list[str]:
        """Get suggestions for undefined variables, including declared variables."""
        declared_vars = list(self._symbols.keys())
        return self._suggestion_engine.get_suggestions(name, 'condition', section, declared_vars)

    def map_hook(self, label: str) -> str:
        with self.debug_context("map_hook", label):
            try:
                result = SectionType(label).hook_name
                return result
            except ValueError:
                error = SymbolResolutionError(label, f"Invalid section name: '{label}'")
                valid_sections = [s.value for s in SectionType]
                error.add_symbol_suggestion(valid_sections)
                raise error
