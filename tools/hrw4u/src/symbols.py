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

#
# Symbol Resolution
#


class SymbolResolver(SymbolResolverBase):
    """Resolves hrw4u symbols to their corresponding header_rewrite operations."""

    def __init__(self, debug: bool = SystemDefaults.DEFAULT_DEBUG) -> None:
        super().__init__(debug)
        self._symbols: dict[str, types.Symbol] = {}
        self._var_counter = {vt: 0 for vt in types.VarType}

    def symbol_for(self, name: str) -> types.Symbol | None:
        return self._symbols.get(name)

    def get_statement_spec(self, name: str) -> tuple[str, Callable[[str], None] | None]:
        # Use cached lookup from base class
        if result := self._lookup_statement_function_cached(name):
            return result
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
            # Use cached operator lookup from base class
            for op_key, (commands, validator, uppercase, restricted_sections) in self._operator_map.items():
                if op_key.endswith("."):
                    if name.startswith(op_key):
                        self.validate_section_access(name, section, restricted_sections)
                        qualifier = name[len(op_key):]
                        if uppercase:
                            qualifier = qualifier.upper()
                        if validator:
                            validator(qualifier)

                        if isinstance(commands, (list, tuple)):
                            if value == '""':
                                result = f"{commands[0]} {qualifier}"
                            else:
                                result = f"{commands[1]} {qualifier} {value}"
                        else:
                            result = f"{commands} {qualifier} {value}"
                        return result
                elif name == op_key:
                    self.validate_section_access(name, section, restricted_sections)
                    if validator:
                        validator(value)
                    result = f"{commands} {value}"
                    return result

        if resolved_lhs := self.symbol_for(name):
            if resolved_rhs := self.symbol_for(value):
                if resolved_rhs.var_type != resolved_lhs.var_type:
                    raise SymbolResolutionError(value, f"Type mismatch: {resolved_lhs.var_type} vs {resolved_rhs.var_type}")
                result = resolved_lhs.as_operator(resolved_rhs.as_cond())
                self._dbg.exit(f"=> var-to-var assignment: {result}")
                return result

            Validator.validate_assignment(resolved_lhs.var_type, value, name)
            result = resolved_lhs.as_operator(value)
            self._dbg.exit(f"=> symbol_table: {result}")
            return result

        self._dbg.exit("=> not found")
        error = SymbolResolutionError(name, "Unknown assignment symbol")
        if section:
            error.add_section_context(section.value)
        raise error

    def resolve_condition(self, name: str, section: SectionType | None = None) -> tuple[str, bool]:
        with self.debug_context("resolve_condition", name, section):
            if symbol := self.symbol_for(name):
                return symbol.as_cond(), False

            # Use cached condition lookup from base class
            if condition_info := self._lookup_condition_cached(name):
                tag, _, _, restricted, default_expr, _ = condition_info
                self.validate_section_access(name, section, restricted)
                return tag, default_expr

            # Check prefix matches using base class utility
            prefix_matches = self.find_prefix_matches(name, self._condition_map)
            for prefix, (tag, validator, uppercase, restricted, default_expr, _) in prefix_matches:
                self.validate_section_access(name, section, restricted)
                suffix = name[len(prefix):]
                suffix_norm = suffix.upper() if uppercase else suffix
                if validator:
                    validator(suffix_norm)
                resolved = f"%{{{tag}:{suffix_norm}}}"
                return resolved, default_expr

            error = SymbolResolutionError(name, "Unknown condition symbol")
            if section:
                error.add_section_context(section.value)
            raise error

    def resolve_function(self, func_name: str, args: list[str], strip_quotes: bool = False) -> str:
        with self.debug_context("resolve_function", func_name, args):
            # Use cached function lookup from base class
            if function_info := self._lookup_function_cached(func_name):
                tag, validator = function_info
                if validator:
                    validator(args)

                cleaned_args = []
                for arg in args:
                    if strip_quotes and arg.startswith('"') and arg.endswith('"'):
                        cleaned_args.append(arg[1:-1])
                    else:
                        cleaned_args.append(arg)
                if cleaned_args:
                    result = f"%{{{tag}:{','.join(cleaned_args)}}}"
                else:
                    result = f"%{{{tag}}}"
                return result

            error = SymbolResolutionError(func_name, f"Unknown function: '{func_name}'")
            error.add_note(f"Arguments provided: {len(args)}")
            raise error

    def resolve_statement_func(self, func_name: str, args: list[str]) -> str:
        with self.debug_context("resolve_statement_func", func_name, args):
            # Use cached statement function lookup from base class
            if function_info := self._lookup_statement_function_cached(func_name):
                command, validator = function_info
                if validator:
                    validator(args)
                if not args:
                    result = command
                else:
                    result = f"{command} {' '.join(args)}"
                    # TODO: Move this special case to states.py module
                    if func_name == "keep_query":
                        result += " [I]"
                return result

            error = SymbolResolutionError(func_name, f"Unknown statement function: '{func_name}'")
            error.add_note(f"Arguments provided: {len(args)}")
            raise error


#
# Hook Mapping
#

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
