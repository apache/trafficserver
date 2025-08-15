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
from typing import FrozenSet


class BooleanLiteral(str, Enum):
    """Boolean literal values used in conditions"""
    TRUE = "TRUE"
    FALSE = "FALSE"

    @classmethod
    def contains(cls, value: str) -> bool:
        return value.upper() in {member.value for member in cls}


class SuffixGroup(Enum):
    URL_FIELDS = frozenset({"SCHEME", "HOST", "PORT", "PATH", "QUERY", "URL"})
    GEO_FIELDS = frozenset({"COUNTRY", "COUNTRY-ISO", "ASN", "ASN-NAME"})
    CONN_FIELDS = frozenset(
        {"LOCAL-ADDR", "LOCAL-PORT", "REMOTE-ADDR", "REMOTE-PORT", "TLS", "H2", "IPV4", "IPV6", "IP-FAMILY", "STACK"})
    HTTP_CNTL_FIELDS = frozenset(
        {"LOGGING", "INTERCEPT_RETRY", "RESP_CACHEABLE", "REQ_CACHEABLE", "SERVER_NO_STORE", "TXN_DEBUG", "SKIP_REMAP"})
    ID_FIELDS = frozenset({"REQUEST", "PROCESS", "UNIQUE"})
    DATE_FIELDS = frozenset({"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "WEEKDAY", "YEARDAY"})
    BOOL_FIELDS = frozenset({"TRUE", "FALSE", "YES", "NO", "ON", "OFF", "0", "1"})

    def validate(self, suffix: str) -> None:
        """Validate that suffix is allowed for this group."""
        allowed_upper = {val.upper() for val in self.value}
        if suffix.upper() not in allowed_upper:
            raise ValueError(
                f"Invalid suffix '{suffix}' for group '{self.name}'. "
                f"Must be one of: {', '.join(sorted(self.value))}")


class VarType(Enum):
    BOOL = ("bool", "FLAG", "set-state-flag", 16)
    INT8 = ("int8", "INT8", "set-state-int8", 4)
    INT16 = ("int16", "INT16", "set-state-int16", 1)

    def __init__(self, name: str, cond_tag: str, op_tag: str, limit: int) -> None:
        self._name = name
        self._cond_tag = cond_tag
        self._op_tag = op_tag
        self._limit = limit

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

    @classmethod
    def from_str(cls, type_str: str) -> 'VarType':
        """Create VarType from string representation."""
        for vt in cls:
            if vt._name == type_str.lower():
                return vt
        raise ValueError(f"Unknown VarType string: {type_str}")


@dataclass(slots=True, frozen=True)
class Symbol:
    var_type: VarType
    index: int

    def as_cond(self) -> str:
        return f"%{{STATE-{self.var_type.cond_tag}:{self.index}}}"

    def as_operator(self, value: str) -> str:
        return f"{self.var_type.op_tag} {self.index} {value}"
