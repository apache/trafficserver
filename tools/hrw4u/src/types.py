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
from enum import Enum
from dataclasses import dataclass
from typing import FrozenSet


class SuffixGroup(Enum):
    URL_FIELDS = frozenset({"SCHEME", "HOST", "PORT", "PATH", "QUERY", "URL"})
    GEO_FIELDS = frozenset({"COUNTRY", "COUNTRY-ISO", "ASN", "ASN-NAME"})
    CONN_FIELDS = frozenset(
        {"LOCAL-ADDR", "LOCAL-PORT", "REMOTE-ADDR", "REMOTE-PORT", "TLS", "H2", "IPV4", "IPV6", "IP-FAMILY", "STACK"})
    HTTP_CNTL_FIELDS = frozenset(
        {"LOGGING", "INTERCEPT_RETRY", "RESP_CACHEABLE", "REQ_CACHEABLE", "SERVER_NO_STORE", "TXN_DEBUG", "SKIP_REMAP"})
    ID_FIELDS = frozenset({"REQUEST", "PROCESS", "UNIQUE"})
    DATE_FIELDS = frozenset({"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "WEEKDAY", "YEARDAY"})
    BOOL_FIELDS = frozenset({"TRUE", "FALSE", "YES", "NO", "On", "Off", "0", "1"})

    def allowed_values(self) -> FrozenSet[str]:
        return self.value

    def validate(self, suffix: str) -> None:
        allowed_upper = {v.upper() for v in self.value}
        if suffix.upper() not in allowed_upper:
            raise ValueError(
                f"Invalid suffix '{suffix}' for group '{self.name}'. "
                f"Must be one of: {', '.join(sorted(self.value))}")


class VarType(Enum):
    BOOL = ("bool", "FLAG", "set-state-flag", 16)
    INT8 = ("int8", "INT8", "set-state-int8", 4)
    INT16 = ("int16", "INT16", "set-state-int16", 1)

    def __init__(self, type_str: str, cond_tag: str, op_prefix: str, limit: int):
        self._str = type_str
        self._cond_tag = cond_tag
        self._op_prefix = op_prefix
        self._limit = limit

    @property
    def tag(self) -> str:
        return self._cond_tag

    @property
    def operator_prefix(self) -> str:
        return self._op_prefix

    @property
    def limit(self) -> int:
        return self._limit

    @property
    def type_str(self) -> str:
        return self._str

    @classmethod
    def from_str(cls, s: str) -> 'VarType':
        s_norm = s.lower()
        for vt in cls:
            if vt.type_str.lower() == s_norm:
                return vt
        raise ValueError(f"Unknown VarType string: {s}")


@dataclass
class Symbol:
    var_type: VarType
    index: int

    def as_cond(self) -> str:
        return f"%{{STATE-{self.var_type.tag}:{self.index}}}"

    def as_operator(self, value) -> str:
        return f"{self.var_type.operator_prefix} {self.index} {value}"
