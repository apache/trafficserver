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
from hrw4u.debugging import Dbg
import hrw4u.types as types
import hrw4u.tables as tables
from hrw4u.states import SectionType
from hrw4u.common import SectionValidator, SystemDefaults


class SymbolResolver:
    """Resolves hrw4u symbols to their corresponding header_rewrite operations."""

    def __init__(self, debug: bool = SystemDefaults.DEFAULT_DEBUG) -> None:
        self._symbols: dict[str, types.Symbol] = {}
        self._var_counter = {vt: 0 for vt in types.VarType}
        self._dbg = Dbg(debug)

    def _check_section(self, name: str, section: SectionType | None, restricted: set[SectionType] | None) -> None:
        SectionValidator.validate_section_access(name, section, restricted)

    def symbol_for(self, name: str) -> types.Symbol | None:
        return self._symbols.get(name)

    def get_statement_spec(self, name: str) -> tuple[str, Callable[[str], None] | None]:
        try:
            return tables.STATEMENT_FUNCTION_MAP[name]
        except KeyError:
            raise SymbolResolutionError(name, "Unknown operator or invalid standalone use")

    def declare_variable(self, name: str, type_name: str) -> str:
        try:
            var_type = types.VarType.from_str(type_name)
        except ValueError:
            raise SymbolResolutionError(name, f"Invalid type '{type_name}'")
        if self._var_counter[var_type] >= var_type.limit:
            raise SymbolResolutionError(name, f"Too many '{type_name}' variables (max {var_type.limit})")

        symbol = types.Symbol(var_type, self._var_counter[var_type])
        self._var_counter[var_type] += 1
        self._symbols[name] = symbol
        return symbol.as_cond()

    def resolve_assignment(self, name: str, value: str, section: SectionType | None = None) -> str:
        self._dbg.enter(f"resolve_assignment: {name} = {value} (section={section})")

        for op_key, (commands, validator, uppercase, restricted_sections) in tables.OPERATOR_MAP.items():
            if op_key.endswith("."):
                if name.startswith(op_key):
                    self._check_section(name, section, restricted_sections)
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
                    self._dbg.exit(f"=> prefix-operator: {result}")
                    return result
            elif name == op_key:
                self._check_section(name, section, restricted_sections)
                if validator:
                    validator(value)
                result = f"{commands} {value}"
                self._dbg.exit(f"=> operator: {result}")
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
        raise SymbolResolutionError(name, "Unknown assignment symbol")

    def resolve_condition(self, name: str, section: SectionType | None = None) -> tuple[str, bool]:
        self._dbg.enter(f"resolve_condition: {name} (section={section})")

        if symbol := self.symbol_for(name):
            self._dbg.exit(f"=> symbol_table: {symbol.as_cond()}")
            return symbol.as_cond(), False

        if name in tables.CONDITION_MAP:
            tag, _, _, restricted, default_expr, _ = tables.CONDITION_MAP[name]
            self._check_section(name, section, restricted)
            self._dbg.exit(f"=> exact condition_map: {tag}")
            return tag, default_expr

        for prefix, (tag, validator, uppercase, restricted, default_expr, _) in tables.CONDITION_MAP.items():
            if prefix.endswith(".") and name.startswith(prefix):
                self._check_section(name, section, restricted)
                suffix = name[len(prefix):]
                suffix_norm = suffix.upper() if uppercase else suffix
                if validator:
                    validator(suffix_norm)
                resolved = f"%{{{tag}:{suffix_norm}}}"
                self._dbg.exit(f"=> prefix condition_map: {resolved}")
                return resolved, default_expr

        self._dbg.exit("=> not found")
        raise SymbolResolutionError(name, "Unknown condition symbol")

    def resolve_function(self, func_name: str, args: list[str], strip_quotes: bool = False) -> str:
        self._dbg.enter(f"resolve_function: {func_name}({', '.join(args)})")

        if func_name not in tables.FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown function: '{func_name}'")

        tag, validator = tables.FUNCTION_MAP[func_name]
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
        self._dbg.exit(f"=> resolved function: {result}")
        return result

    def resolve_statement_func(self, func_name: str, args: list[str]) -> str:
        self._dbg.enter(f"resolve_statement_func: {func_name}({', '.join(args)})")

        if func_name not in tables.STATEMENT_FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown statement function: '{func_name}'")

        command, validator = tables.STATEMENT_FUNCTION_MAP[func_name]
        if validator:
            validator(args)
        if not args:
            result = command
        else:
            result = f"{command} {' '.join(args)}"
            # TODO: Move this special case to states.py module
            if func_name == "keep_query":
                result += " [I]"
        self._dbg.exit(f"=> resolved statement function: {result}")
        return result

    def map_hook(self, label: str) -> str:
        self._dbg.enter(f"map_hook: {label}")
        try:
            result = SectionType(label).hook_name
        except ValueError:
            raise SymbolResolutionError(label, f"Invalid section name: '{label}'")
        self._dbg.exit(f"=> mapped: {result}")
        return result
