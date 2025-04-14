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
import sys
from typing import Dict, List, Set, Tuple, Optional, Callable
from hrw4u.validation import Validator
from hrw4u.errors import SymbolResolutionError
import hrw4u.types as types


class SymbolResolver:
    # This map is for operators, which may have duplicates as in the _CONDITIONS_MAP. Note that
    # not all of conditions are valid operators.
    _OPERATOR_MAP: Dict[str, Tuple[str | List[str], Optional[Callable[[str], None]], bool, Optional[Set[str]]]] = {
        "cookie.": (["rm-cookie", "set-cookie"], Validator.quoted_or_simple(), False, None),
        "http.cntl.": ("set-http-cntl", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None),
        "http.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
        "http.status": ("set-status", Validator.range(0, 999), False, None),
        "inbound.conn.dscp": ("set-conn-dscp", Validator.nbit_int(6), False, None),
        "inbound.req.": (["rm-header", "set-header"], Validator.quoted_or_simple(), False, None),
        "inbound.resp.body": ("set-body", Validator.quoted_or_simple(), False, None),
        "inbound.resp.": (["rm-header", "set-header"], Validator.quoted_or_simple(), False, None),
        "inbound.status.reason": ("set-status-reason", Validator.range(0, 999), False, None),
        "inbound.status": ("set-status", Validator.range(0, 999), False, None),
        "inbound.url.": (["rm-destination", "set-destination"], Validator.quoted_or_simple(), True, None),
        "outbound.req.": (["rm-header", "set-header"], Validator.quoted_or_simple(), False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.resp.":
            (
                ["rm-header",
                 "set-header"], Validator.quoted_or_simple(), False, {"PRE_REMAP", "REMAP", "READ_REQUEST", "SEND_REQUEST"}),
        "outbound.status.reason": ("set-status-reason", Validator.range(0, 999), False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.status": ("set-status", Validator.range(0, 999), False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
    }

    # This map is for functions which can never be used as conditions. We split this out to avoid
    # the conflict that otherwise happens when a function name is also a condition name.
    _STATEMENT_FUNCTION_MAP: Dict[str, Tuple[str, Optional[Callable[[List[str]], None]]]] = {
        "counter": ("counter", Validator.arg_count(1).quoted_or_simple()),
        "set-debug": ("set-debug", Validator.arg_count(0)),
        "no-op": ("no-op", Validator.arg_count(0)),
        "remove_query": ("rm-destination QUERY", Validator.arg_count(1).quoted_or_simple()),
        "keep_query": ("rm-destination QUERY", Validator.arg_count(1).quoted_or_simple()),
        "run-plugin": ("run-plugin", Validator.min_args(1).quoted_or_simple()),
        "set-body-from": ("set-body-from", Validator.arg_count(1).quoted_or_simple()),
        "set-config": ("set-config", Validator.arg_count(2).quoted_or_simple()),
        "set-redirect":
            ("set-redirect", Validator.arg_count(2).arg_at(0, Validator.range(300, 399)).arg_at(1, Validator.quoted_or_simple())),
        "skip-remap": ("skip-remap", Validator.arg_count(1).suffix_group(types.SuffixGroup.BOOL_FIELDS)),
    }

    # The function map are for the hybrid functions which map to what looks like conditions,
    # but don't map nicely to the X.y syntax we prefer in HRW4U.
    _FUNCTION_MAP = {
        "access": ("ACCESS", Validator.arg_count(1).quoted_or_simple()),
        "cache": ("CACHE", Validator.arg_count(0)),
        "cidr": ("CIDR", Validator.arg_count(2).arg_at(0, Validator.range(1, 32)).arg_at(1, Validator.range(1, 128))),
        "internal": ("INTERNAL-TRANSACTION", Validator.arg_count(0)),
        "random": ("RANDOM", Validator.arg_count(1).nbit_int(32)),
        "ssn-txn-count": ("SSN-TXN-COUNT", Validator.arg_count(0)),
        "txn-count": ("TXN-COUNT", Validator.arg_count(0)),
    }

    _CONDITION_MAP: Dict[str, Tuple[str, Optional[Callable[[str], None]], bool, Optional[Set[str]]]] = {
        # Exact matches
        "inbound.ip": ("%{IP:CLIENT}", None, False, None),
        "inbound.method": ("%{METHOD}", None, False, None),
        "inbound.server": ("%{IP:INBOUND}", None, False, None),
        "inbound.status": ("%{STATUS}", None, False, None),
        "now": ("%{NOW}", None, False, None),
        "outbound.ip": ("%{IP:SERVER}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.method": ("%{METHOD}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.server": ("%{IP:OUTBOUND}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.status": ("%{STATUS}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "tcp.info": ("%{TCP-INFO}", None, False, None),

        # Prefix matches
        "capture.": ("LAST-CAPTURE", Validator.range(0, 9), False, None),
        "client.cert.": ("CLIENT-CERT", None, True, None),
        "cookie.": ("COOKIE", Validator.quoted_or_simple(), False, None),
        "from.url.": ("FROM-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
        "geo.": ("GEO", Validator.suffix_group(types.SuffixGroup.GEO_FIELDS), True, None),
        "http.cntl.": ("HTTP-CNTL", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None),
        "id.": ("ID", Validator.suffix_group(types.SuffixGroup.ID_FIELDS), True, None),
        "inbound.conn.": ("INBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None),
        "inbound.req.": ("CLIENT-HEADER", None, False, None),
        "inbound.resp.": ("HEADER", None, False, None),
        "inbound.url.": ("CLIENT-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
        "now.": ("NOW", Validator.suffix_group(types.SuffixGroup.DATE_FIELDS), True, None),
        "outbound.conn.": ("OUTBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None),
        "outbound.req.": ("HEADER", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.resp.": ("HEADER", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST", "SEND_REQUEST"}),
        "outbound.url.":
            ("NEXT-HOP", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "to.url.": ("TO-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
    }

    _SECTION_MAP = {
        "REMAP": "REMAP_PSEUDO_HOOK",
        "SEND_REQUEST": "SEND_REQUEST_HDR_HOOK",
        "READ_RESPONSE": "READ_RESPONSE_HDR_HOOK",
        "SEND_RESPONSE": "SEND_RESPONSE_HDR_HOOK",
        "READ_REQUEST": "READ_REQUEST_HDR_HOOK",
        "PRE_REMAP": "READ_REQUEST_PRE_REMAP_HOOK",
        "TXN_START": "TXN_START_HOOK",
        "TXN_CLOSE": "TXN_CLOSE_HOOK",
    }

    def __init__(self):
        self._symbols: Dict[str, types.Symbol] = {}
        self._var_counter = {vt: 0 for vt in types.VarType}
        self._debug_enabled = False
        self._debug_indent = 0

    def _debug(self, msg: str):
        if self._debug_enabled:
            print(f">>> [symbols] {'  ' * self._debug_indent}{msg}", file=sys.stderr)

    def _debug_enter(self, msg: str):
        self._debug(msg)
        self._debug_indent += 1

    def _debug_exit(self, msg: Optional[str] = None):
        self._debug_indent = max(0, self._debug_indent - 1)
        if msg:
            self._debug(msg)

    # Mostly for testing
    def inject_symbol(self, name: str, symbol: types.Symbol) -> None:
        self._symbols[name] = symbol

    def symbol_for(self, name: str) -> Optional[types.Symbol]:
        return self._symbols.get(name)

    def check_section(self, name: str, section: Optional[str], restricted: Optional[Set[str]]):
        if section and restricted and section in restricted:
            raise SymbolResolutionError(name, f"{name} is not available in the {section} section")

    def get_operator(self, name: str) -> Tuple[str, Optional[Callable[[str], None]]]:
        try:
            return self._OPERATOR_MAP[name]
        except KeyError:
            raise SymbolResolutionError(name, "Unknown operator or invalid standalone use")

    def declare_variable(self, name: str, typ_str: str) -> str:
        try:
            var_type = types.VarType.from_str(typ_str)
        except ValueError:
            raise SymbolResolutionError(name, f"Invalid type '{typ_str}'")
        if self._var_counter[var_type] >= var_type.limit:
            raise SymbolResolutionError(name, f"Too many '{typ_str}' variables (max {var_type.limit})")

        idx = self._var_counter[var_type]
        self._var_counter[var_type] += 1
        symbol = types.Symbol(var_type, idx)
        self._symbols[name] = symbol
        return symbol.as_cond()

    def resolve_assignment(self, name: str, value: str, section: Optional[str] = None) -> str:
        self._debug_enter(f"resolve_assignment: {name} = {value} (section={section})")

        for op_key, (commands, validator, uppercase, restricted_sections) in self._OPERATOR_MAP.items():
            if op_key.endswith("."):
                if name.startswith(op_key):
                    self.check_section(name, section, restricted_sections)
                    qualifier = name[len(op_key):]
                    if uppercase:
                        qualifier = qualifier.upper()
                    if isinstance(commands, list):
                        cmd = commands[0] if value == '""' else commands[1]
                    else:
                        cmd = commands
                    result = f"{cmd} {qualifier}" if value == '""' else f"{cmd} {qualifier} {value}"
                    self._debug_exit(f"=> prefix-operator: {result}")
                    return result
            else:
                if name == op_key:
                    self.check_section(name, section, restricted_sections)
                    validator and validator(value)
                    result = f"{commands} {value}"
                    self._debug_exit(f"=> operator: {result}")
                    return result

        resolved = self.symbol_for(name)
        if resolved:
            Validator.validate_var_assignment(resolved.var_type, value, name)
            result = resolved.as_operator(value)
            self._debug_exit(f"=> symbol_table: {result}")
            return result

        self._debug_exit("=> not found")
        raise SymbolResolutionError(name, "Unknown assignment symbol")

    def resolve_condition(self, name: str, section: Optional[str] = None) -> str:
        self._debug_enter(f"resolve_condition: {name} (section={section})")

        symbol = self.symbol_for(name)
        if symbol:
            self._debug_exit(f"=> symbol_table: {symbol.as_cond()}")
            return symbol.as_cond()

        # Exact match
        if name in self._CONDITION_MAP:
            tag, _, _, restricted = self._CONDITION_MAP[name]
            self.check_section(name, section, restricted)
            self._debug_exit(f"=> exact condition_map: {tag}")
            return tag

        # Prefix match
        for prefix, (tag, validator, uppercase, restricted) in self._CONDITION_MAP.items():
            if prefix.endswith(".") and name.startswith(prefix):
                self.check_section(name, section, restricted)
                suffix = name[len(prefix):]
                suffix_transformed = suffix.upper() if uppercase else suffix
                validator and validator(suffix_transformed)
                resolved = f"%{{{tag}:{suffix_transformed}}}"
                self._debug_exit(f"=> prefix condition_map: {resolved}")
                return resolved

        self._debug_exit("=> not found")
        raise SymbolResolutionError(name, "Unknown condition symbol")

    def resolve_function(self, func_name: str, args: List[str], strip_quotes: bool = False) -> str:
        self._debug_enter(f"resolve_function: {func_name}({', '.join(args)})")

        if func_name not in self._FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown function: '{func_name}'")

        tag, validator = self._FUNCTION_MAP[func_name]
        validator and validator(args)
        cleaned_args = [a[1:-1] if strip_quotes and a.startswith('"') and a.endswith('"') else a for a in args]

        result = f"%{{{tag}}}" if not cleaned_args else f"%{{{tag}:{','.join(cleaned_args)}}}"
        self._debug_exit(f"=> resolved function: {result}")
        return result

    def resolve_statement_func(self, func_name: str, args: List[str]) -> str:
        self._debug_enter(f"resolve_statement_func: {func_name}({', '.join(args)})")

        if func_name not in self._STATEMENT_FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown statement function: '{func_name}'")

        command, validator = self._STATEMENT_FUNCTION_MAP[func_name]
        validator and validator(args)

        if not args:
            result = command
        else:
            result = f"{command} {' '.join(args)}"
            if func_name == "keep_query":
                result += " [I]"

        self._debug_exit(f"=> resolved statement function: {result}")
        return result

    def map_hook(self, label: str) -> str:
        self._debug_enter(f"map_hook: {label}")
        if label not in self._SECTION_MAP:
            raise SymbolResolutionError(label, f"Invalid section name: '{label}'")
        result = self._SECTION_MAP[label]
        self._debug_exit(f"=> mapped: {result}")
        return result

    def validate(self) -> Dict[str, List[str] | Dict[str, List[str]]]:
        return {
            "symbol_table": sorted(self._symbols),
            "variables": sorted(f"{name}: {sym.var_type.name.lower()}" for name, sym in self._symbols.items()),
            "condition_map": sorted(self._CONDITION_MAP),
            "functions": sorted(self._FUNCTION_MAP),
            "sections": sorted(self._SECTION_MAP),
            "operator_map": sorted(self._OPERATOR_MAP),
            "suffix_groups": {
                group.name.lower(): sorted(group.allowed_values()) for group in types.SuffixGroup
            },
        }
