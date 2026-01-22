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

from enum import Enum
from dataclasses import dataclass
from typing import Self, Callable, TYPE_CHECKING

if TYPE_CHECKING:
    from hrw4u.states import SectionType


class MagicStrings(str, Enum):
    ADD_HEADER = "add-header"
    RM_HEADER = "rm-header"
    SET_HEADER = "set-header"
    RM_COOKIE = "rm-cookie"
    SET_COOKIE = "set-cookie"
    RM_DESTINATION = "rm-destination"
    SET_DESTINATION = "set-destination"
    SET_STATUS = "set-status"
    SET_STATUS_REASON = "set-status-reason"
    SET_BODY = "set-body"
    SET_BODY_FROM = "set-body-from"
    NO_OP = "no-op"
    SET_DEBUG = "set-debug"
    SET_CC_ALG = "set-cc-alg"
    SET_CONFIG = "set-config"
    SET_EFFECTIVE_ADDRESS = "set-effective-address"
    SET_REDIRECT = "set-redirect"
    SKIP_REMAP = "skip-remap"
    RUN_PLUGIN = "run-plugin"
    COUNTER = "counter"
    SET_CONN_DSCP = "set-conn-dscp"
    SET_HTTP_CNTL = "set-http-cntl"
    REMOVE_QUERY = "remove_query"
    KEEP_QUERY = "keep_query"


class BooleanLiteral(str, Enum):
    TRUE = "TRUE"
    FALSE = "FALSE"

    @classmethod
    def contains(cls, value: str) -> bool:
        return value.upper() in {member.value for member in cls}


class LanguageKeyword(Enum):
    IF = ("if", "Conditional statement")
    ELIF = ("elif", "Else-if clause")
    ELSE = ("else", "Else clause")
    WITH = ("with", "Condition modifier keyword")
    IN = ("in", "Set membership operator")
    TRUE = ("true", "Boolean literal")
    FALSE = ("false", "Boolean literal")
    BREAK = ("break", "Loop break statement")

    def __init__(self, keyword: str, description: str) -> None:
        self.keyword = keyword
        self.description = description

    @classmethod
    def get_keywords_with_descriptions(cls) -> dict[str, str]:
        return {member.keyword: member.description for member in cls}


class SuffixGroup(Enum):
    URL_FIELDS = frozenset({"SCHEME", "HOST", "PORT", "PATH", "QUERY", "URL"})
    GEO_FIELDS = frozenset({"COUNTRY", "COUNTRY-ISO", "ASN", "ASN-NAME"})
    CONN_FIELDS = frozenset(
        {
            "LOCAL-ADDR", "LOCAL-PORT", "REMOTE-ADDR", "REMOTE-PORT", "TLS", "H2", "IPV4", "IPV6", "IP-FAMILY", "STACK",
            "client-cert", "server-cert"
        })
    HTTP_CNTL_FIELDS = frozenset(
        {"LOGGING", "INTERCEPT_RETRY", "RESP_CACHEABLE", "REQ_CACHEABLE", "SERVER_NO_STORE", "TXN_DEBUG", "SKIP_REMAP"})
    ID_FIELDS = frozenset({"REQUEST", "PROCESS", "UNIQUE"})
    DATE_FIELDS = frozenset({"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "WEEKDAY", "YEARDAY"})
    BOOL_FIELDS = frozenset({"TRUE", "FALSE", "YES", "NO", "ON", "OFF", "0", "1"})
    CERT_FIELDS = frozenset(
        {
            "PEM", "pem", "SIG", "sig", "SUBJECT", "subject", "ISSUER", "issuer", "SERIAL", "serial", "NOT_BEFORE", "not_before",
            "NOT_AFTER", "not_after", "VERSION", "version"
        })
    SAN_FIELDS = frozenset({"DNS", "dns", "IP", "ip", "EMAIL", "email", "URI", "uri"})
    PLUGIN_CNTL_MAPPING = {"TIMEZONE": frozenset({"GMT", "LOCAL"}), "INBOUND_IP_SOURCE": frozenset({"PEER", "PROXY"})}
    PLUGIN_CNTL_FIELDS = frozenset(PLUGIN_CNTL_MAPPING.keys())

    def validate(self, suffix: str) -> None:
        allowed_upper = {val.upper() for val in self.value}
        if suffix.upper() not in allowed_upper:
            # Special handling for BOOL_FIELDS to detect quoted boolean values
            if self == SuffixGroup.BOOL_FIELDS:
                bool_msg = f"Invalid boolean value '{suffix}'. Must be one of: {', '.join(sorted(self.value))}"

                # Check if this is a quoted boolean value
                is_double_quoted = (suffix.startswith('"') and suffix.endswith('"'))
                is_single_quoted = (suffix.startswith("'") and suffix.endswith("'"))
                if ((is_double_quoted or is_single_quoted) and len(suffix) > 2 and suffix[1:-1].upper() in allowed_upper):
                    raise ValueError(f"{bool_msg} and must not be quoted")
                else:
                    raise ValueError(bool_msg)
            else:
                raise ValueError(
                    f"Invalid suffix '{suffix}' for group '{self.name}'. "
                    f"Must be one of: {', '.join(sorted(self.value))}")


class VarType(Enum):
    BOOL = ("bool", "FLAG", "set-state-flag", 16, "Boolean variable type - stores true/false values")
    INT8 = ("int8", "INT8", "set-state-int8", 4, "8-bit integer variable type - stores values from 0 to 255")
    INT16 = ("int16", "INT16", "set-state-int16", 1, "16-bit integer variable type - stores values from 0 to 65535")

    def __init__(self, name: str, cond_tag: str, op_tag: str, limit: int, description: str) -> None:
        self._name = name
        self._cond_tag = cond_tag
        self._op_tag = op_tag
        self._limit = limit
        self._description = description

    @property
    def cond_tag(self) -> str:
        return self._cond_tag

    @property
    def op_tag(self) -> str:
        return self._op_tag

    @property
    def limit(self) -> int:
        return self._limit

    @property
    def name(self) -> str:
        return self._name

    @property
    def description(self) -> str:
        return self._description

    @classmethod
    def from_str(cls, type_str: str) -> Self:
        for vt in cls:
            if vt._name == type_str.lower():
                return vt
        raise ValueError(f"Unknown VarType string: {type_str}")


@dataclass(slots=True, frozen=True)
class Symbol:
    var_type: VarType
    slot: int

    def as_cond(self) -> str:
        return f"%{{STATE-{self.var_type.cond_tag}:{self.slot}}}"

    def as_operator(self, value: str) -> str:
        return f"{self.var_type.op_tag} {self.slot} {value}"


class MapParams:
    """Map parameters for table entries combining flags and metadata.
    """

    def __init__(
            self,
            upper: bool = False,
            add: bool = False,
            prefix: bool = False,
            validate: Callable[[str], None] | None = None,
            sections: set[SectionType] | None = None,
            rev: dict | None = None,
            target: str | list[str] | tuple[str, ...] | None = None) -> None:
        object.__setattr__(
            self, '_params', {
                'upper': upper,
                'add': add,
                'prefix': prefix,
                'validate': validate,
                'sections': sections,
                'rev': rev,
                'target': target
            })

    def __getattr__(self, name: str):
        if name.startswith('_'):
            raise AttributeError(f"'{type(self).__name__}' object has no attribute '{name}'")
        return self._params.get(name, False if name in ('upper', 'add', 'prefix') else None)

    def __setattr__(self, name: str, value: object) -> None:
        """Prevent modification after initialization (immutable)."""
        raise AttributeError(f"'{type(self).__name__}' object is immutable")

    def __repr__(self) -> str:
        non_defaults = []
        for k, v in self._params.items():
            if k in ('upper', 'add', 'prefix'):
                if v:
                    non_defaults.append(f"{k}=True")
            elif v is not None:
                if isinstance(v, set):
                    non_defaults.append(f"{k}={{{', '.join(str(s) for s in v)}}}")
                elif k == 'validate':
                    non_defaults.append(f"{k}=<validator>")
                else:
                    non_defaults.append(f"{k}=...")

        if not non_defaults:
            return "MapParams()"
        return f"MapParams({', '.join(non_defaults)})"

    def __hash__(self) -> int:
        hashable_items = []
        for k, v in self._params.items():
            if isinstance(v, set):
                hashable_items.append((k, frozenset(v)))
            elif isinstance(v, dict):
                hashable_items.append((k, frozenset(v.items()) if v else None))
            elif callable(v):
                hashable_items.append((k, id(v)))
            else:
                hashable_items.append((k, v))
        return hash(frozenset(hashable_items))

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, MapParams):
            return NotImplemented
        return self._params == other._params
