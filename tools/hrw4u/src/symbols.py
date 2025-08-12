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
        "http.cntl.": ("set-http-cntl", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None),
        "http.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
        "http.status": ("set-status", Validator.range(0, 999), False, None),
        "inbound.conn.dscp": ("set-conn-dscp", Validator.nbit_int(6), False, None),
        "inbound.cookie.": (["rm-cookie", "set-cookie"], Validator.http_token(), False, None),
        "inbound.req.": (["rm-header", "set-header"], Validator.http_token(), False, None),
        "inbound.resp.body": ("set-body", Validator.quoted_or_simple(), False, None),
        "inbound.resp.": (["rm-header", "set-header"], Validator.http_token(), False, None),
        "inbound.status.reason": ("set-status-reason", Validator.range(0, 999), False, None),
        "inbound.status": ("set-status", Validator.range(0, 999), False, None),
        "inbound.url.": (["rm-destination", "set-destination"], Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
        "outbound.cookie.": (["rm-cookie", "set-cookie"], Validator.http_token(), False, None),
        "outbound.req.": (["rm-header", "set-header"], Validator.http_token(), False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
        "outbound.resp.":
            (["rm-header", "set-header"], Validator.http_token(), False, {"PRE_REMAP", "REMAP", "READ_REQUEST", "SEND_REQUEST"}),
        "outbound.status.reason":
            ("set-status-reason", Validator.quoted_or_simple(), False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}),
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

    _CONDITION_MAP: Dict[str, Tuple[str, Optional[Callable[[str], None]], bool, Optional[Set[str]], bool]] = {
        # Exact matches
        "inbound.ip": ("%{IP:CLIENT}", None, False, None, False),
        "inbound.method": ("%{METHOD}", None, False, None, False),
        "inbound.server": ("%{IP:INBOUND}", None, False, None, False),
        "inbound.status": ("%{STATUS}", None, False, None, False),
        "now": ("%{NOW}", None, False, None, False),
        "outbound.ip": ("%{IP:SERVER}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, False),
        "outbound.method": ("%{METHOD}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, False),
        "outbound.server": ("%{IP:OUTBOUND}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, False),
        "outbound.status": ("%{STATUS}", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, False),
        "tcp.info": ("%{TCP-INFO}", None, False, None, False),

        # Prefix matches
        "capture.": ("LAST-CAPTURE", Validator.range(0, 9), False, None, True),
        "client.cert.": ("CLIENT-CERT", None, True, None, True),
        "from.url.": ("FROM-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True),
        "geo.": ("GEO", Validator.suffix_group(types.SuffixGroup.GEO_FIELDS), True, None, True),
        "http.cntl.": ("HTTP-CNTL", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None, False),
        "id.": ("ID", Validator.suffix_group(types.SuffixGroup.ID_FIELDS), True, None, False),
        "inbound.conn.": ("INBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None, True),
        "inbound.cookie.": ("COOKIE", Validator.http_token(), False, None, True),
        "inbound.req.": ("CLIENT-HEADER", None, False, None, True),
        "inbound.resp.": ("HEADER", None, False, None, True),
        "inbound.url.": ("CLIENT-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True),
        "now.": ("NOW", Validator.suffix_group(types.SuffixGroup.DATE_FIELDS), True, None, False),
        "outbound.conn.": ("OUTBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None, True),
        "outbound.cookie.": ("COOKIE", Validator.http_token(), False, None, True),
        "outbound.req.": ("HEADER", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, True),
        "outbound.resp.": ("HEADER", None, False, {"PRE_REMAP", "REMAP", "READ_REQUEST", "SEND_REQUEST"}, True),
        "outbound.url.":
            ("NEXT-HOP", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, {"PRE_REMAP", "REMAP", "READ_REQUEST"}, True),
        "to.url.": ("TO-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True),
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

    def __init__(self, debug: bool = False):
        self._symbols: Dict[str, types.Symbol] = {}
        self._var_counter = {vt: 0 for vt in types.VarType}
        self._debug_enabled = debug
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

    def declare_variable(self, name: str, type: str) -> str:
        try:
            var_type = types.VarType.from_str(type)
        except ValueError:
            raise SymbolResolutionError(name, f"Invalid type '{type}'")
        if self._var_counter[var_type] >= var_type.limit:
            raise SymbolResolutionError(name, f"Too many '{type}' variables (max {var_type.limit})")

        symbol = types.Symbol(var_type, self._var_counter[var_type])
        self._var_counter[var_type] += 1
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
                    if validator:
                        validator(qualifier)
                    if isinstance(commands, list):  # rm- / -set- operator
                        if value == '""':
                            result = f"{commands[0]} {qualifier}"
                        else:
                            result = f"{commands[1]} {qualifier} {value}"
                    else:
                        result = f"{commands} {qualifier} {value}"
                    self._debug_exit(f"=> prefix-operator: {result}")
                    return result
            else:
                if name == op_key:
                    self.check_section(name, section, restricted_sections)
                    if validator:
                        validator(value)
                    result = f"{commands} {value}"
                    self._debug_exit(f"=> operator: {result}")
                    return result

        resolved = self.symbol_for(name)
        if resolved:
            Validator.validate_assignment(resolved.var_type, value, name)
            result = resolved.as_operator(value)
            self._debug_exit(f"=> symbol_table: {result}")
            return result

        self._debug_exit("=> not found")
        raise SymbolResolutionError(name, "Unknown assignment symbol")

    def resolve_condition(self, name: str, section: Optional[str] = None) -> Tuple[str, bool]:
        self._debug_enter(f"resolve_condition: {name} (section={section})")

        symbol = self.symbol_for(name)
        if symbol:
            self._debug_exit(f"=> symbol_table: {symbol.as_cond()}")
            return symbol.as_cond(), False

        # Exact match
        if name in self._CONDITION_MAP:
            tag, _, _, restricted, default_expr = self._CONDITION_MAP[name]
            self.check_section(name, section, restricted)
            self._debug_exit(f"=> exact condition_map: {tag}")
            return tag, default_expr

        # Prefix match
        for prefix, (tag, validator, uppercase, restricted, default_expr) in self._CONDITION_MAP.items():
            if prefix.endswith(".") and name.startswith(prefix):
                self.check_section(name, section, restricted)
                suffix = name[len(prefix):]
                suf_norm = suffix.upper() if uppercase else suffix
                if validator:
                    validator(suf_norm)
                resolved = f"%{{{tag}:{suf_norm}}}"
                self._debug_exit(f"=> prefix condition_map: {resolved}")
                return resolved, default_expr

        self._debug_exit("=> not found")
        raise SymbolResolutionError(name, "Unknown condition symbol")

    def resolve_function(self, func_name: str, args: List[str], strip_quotes: bool = False) -> str:
        self._debug_enter(f"resolve_function: {func_name}({', '.join(args)})")

        if func_name not in self._FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown function: '{func_name}'")

        tag, validator = self._FUNCTION_MAP[func_name]
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
        self._debug_exit(f"=> resolved function: {result}")
        return result

    def resolve_statement_func(self, func_name: str, args: List[str]) -> str:
        self._debug_enter(f"resolve_statement_func: {func_name}({', '.join(args)})")

        if func_name not in self._STATEMENT_FUNCTION_MAP:
            raise SymbolResolutionError(func_name, f"Unknown statement function: '{func_name}'")

        command, validator = self._STATEMENT_FUNCTION_MAP[func_name]
        if validator:
            validator(args)
        if not args:
            result = command
        else:
            result = f"{command} {' '.join(args)}"
            # Special hack, only the keep_query function needs the [I] modifier right now ...
            # Todo: This should be handled by the states.py module.
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
