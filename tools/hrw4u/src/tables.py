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
from typing import Final
from dataclasses import dataclass
from hrw4u.generators import _table_generator
from hrw4u.generators import get_complete_reverse_resolution_map

from typing import Callable
from hrw4u.validation import Validator
import hrw4u.types as types
from hrw4u.states import SectionType
from hrw4u.common import HeaderOperations

#
# Core Symbol Maps
#

OPERATOR_MAP: dict[str, tuple[str | list[str] | tuple[str, ...], Callable[[str], None] | None, bool, set[SectionType] | None]] = {
    "http.cntl.": ("set-http-cntl", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None),
    "http.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
    "http.status": ("set-status", Validator.range(0, 999), False, None),
    "inbound.conn.dscp": ("set-conn-dscp", Validator.nbit_int(6), False, None),
    "inbound.conn.mark": ("set-conn-mark", Validator.nbit_int(32), False, None),
    "outbound.conn.dscp":
        ("set-conn-dscp", Validator.nbit_int(6), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "outbound.conn.mark":
        ("set-conn-mark", Validator.nbit_int(32), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "inbound.cookie.": (["rm-cookie", "set-cookie"], Validator.http_token(), False, None),
    "inbound.req.": (HeaderOperations.OPERATIONS, Validator.http_token(), False, None),
    "inbound.resp.body": ("set-body", Validator.quoted_or_simple(), False, None),
    "inbound.resp.": (HeaderOperations.OPERATIONS, Validator.http_token(), False, None),
    "inbound.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
    "inbound.status": ("set-status", Validator.range(0, 999), False, None),
    "inbound.url.": (["rm-destination", "set-destination"], Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
    "outbound.cookie.":
        (
            ["rm-cookie",
             "set-cookie"], Validator.http_token(), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "outbound.req.":
        (
            HeaderOperations.OPERATIONS, Validator.http_token(), False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "outbound.resp.":
        (
            HeaderOperations.OPERATIONS, Validator.http_token(), False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}),
    "outbound.status.reason":
        (
            "set-status-reason", Validator.quoted_or_simple(), False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}),
    "outbound.status":
        (
            "set-status", Validator.range(0, 999), False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}),
    "outbound.url.":
        (
            ["rm-destination", "set-destination"], Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST})
}

# This map is for functions which can never be used as conditions. We split this out to avoid
# the conflict that otherwise happens when a function name is also a condition name.
STATEMENT_FUNCTION_MAP: dict[str, tuple[str, Callable[[list[str]], None] | None]] = {
    "add-header": ("add-header", Validator.arg_count(2).arg_at(0, Validator.http_token()).arg_at(1, Validator.quoted_or_simple())),
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
    "skip-remap":
        ("skip-remap", Validator.arg_count(1).suffix_group(types.SuffixGroup.BOOL_FIELDS)._add(Validator.normalize_arg_at(0))),
    "set-plugin-cntl":
        (
            "set-plugin-cntl", Validator.arg_count(2)._add(Validator.normalize_arg_at(0)).arg_at(
                0, Validator.suffix_group(types.SuffixGroup.PLUGIN_CNTL_FIELDS))._add(Validator.normalize_arg_at(1))._add(
                    Validator.conditional_arg_validation(types.SuffixGroup.PLUGIN_CNTL_MAPPING.value))),
}

#
# Condition and Function Maps
#

# The function map are for the hybrid functions which map to what looks like conditions,
# but don't map nicely to the X.y syntax we prefer in HRW4U.
FUNCTION_MAP = {
    "access": ("ACCESS", Validator.arg_count(1).quoted_or_simple()),
    "cache": ("CACHE", Validator.arg_count(0)),
    "cidr": ("CIDR", Validator.arg_count(2).arg_at(0, Validator.range(1, 32)).arg_at(1, Validator.range(1, 128))),
    "internal": ("INTERNAL-TRANSACTION", Validator.arg_count(0)),
    "random": ("RANDOM", Validator.arg_count(1).nbit_int(32)),
    "ssn-txn-count": ("SSN-TXN-COUNT", Validator.arg_count(0)),
    "txn-count": ("TXN-COUNT", Validator.arg_count(0)),
}

CONDITION_MAP: dict[str, tuple[str, Callable[[str], None] | None, bool, set[SectionType] | None, bool, dict | None]] = {
    # Exact matches with reverse mapping info
    "inbound.ip": ("%{IP:CLIENT}", None, False, None, False, {
        "reverse_tag": "IP",
        "reverse_payload": "CLIENT"
    }),
    "inbound.method": ("%{METHOD}", None, False, None, False, {
        "reverse_tag": "METHOD",
        "ambiguous": True
    }),
    "inbound.server": ("%{IP:INBOUND}", None, False, None, False, {
        "reverse_tag": "IP",
        "reverse_payload": "INBOUND"
    }),
    "inbound.status": ("%{STATUS}", None, False, None, False, {
        "reverse_tag": "STATUS",
        "ambiguous": True
    }),
    "now": ("%{NOW}", None, False, None, False, None),
    "outbound.ip":
        (
            "%{IP:SERVER}",
            None,
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            False,
            {
                "reverse_tag": "IP",
                "reverse_payload": "SERVER"
            },
        ),
    "outbound.method":
        (
            "%{METHOD}",
            None,
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            False,
            {
                "reverse_tag": "METHOD",
                "ambiguous": True
            },
        ),
    "outbound.server":
        (
            "%{IP:OUTBOUND}",
            None,
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            False,
            {
                "reverse_tag": "IP",
                "reverse_payload": "OUTBOUND"
            },
        ),
    "outbound.status":
        (
            "%{STATUS}",
            None,
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST},
            False,
            {
                "reverse_tag": "STATUS",
                "ambiguous": True
            },
        ),
    "tcp.info": ("%{TCP-INFO}", None, False, None, False, None),

    # Prefix matches with reverse mapping info
    "capture.": ("LAST-CAPTURE", Validator.range(0, 9), False, None, True, None),
    "from.url.": ("FROM-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True, None),
    "geo.": ("GEO", Validator.suffix_group(types.SuffixGroup.GEO_FIELDS), True, None, True, None),
    "http.cntl.": ("HTTP-CNTL", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None, False, None),
    "id.": ("ID", Validator.suffix_group(types.SuffixGroup.ID_FIELDS), True, None, False, None),
    "inbound.conn.client-cert.SAN.":
        ("INBOUND:CLIENT-CERT:SAN", Validator.suffix_group(types.SuffixGroup.SAN_FIELDS), True, None, True, None),
    "inbound.conn.server-cert.SAN.":
        ("INBOUND:SERVER-CERT:SAN", Validator.suffix_group(types.SuffixGroup.SAN_FIELDS), True, None, True, None),
    "inbound.conn.client-cert.san.":
        ("INBOUND:CLIENT-CERT:SAN", Validator.suffix_group(types.SuffixGroup.SAN_FIELDS), True, None, True, None),
    "inbound.conn.server-cert.san.":
        ("INBOUND:SERVER-CERT:SAN", Validator.suffix_group(types.SuffixGroup.SAN_FIELDS), True, None, True, None),
    "inbound.conn.client-cert.":
        ("INBOUND:CLIENT-CERT", Validator.suffix_group(types.SuffixGroup.CERT_FIELDS), True, None, True, None),
    "inbound.conn.server-cert.":
        ("INBOUND:SERVER-CERT", Validator.suffix_group(types.SuffixGroup.CERT_FIELDS), True, None, True, None),
    "inbound.conn.": ("INBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None, True, None),
    "inbound.cookie.": ("COOKIE", Validator.http_token(), False, None, True, {
        "reverse_fallback": "inbound.cookie."
    }),
    "inbound.req.": ("CLIENT-HEADER", None, False, None, True, {
        "reverse_fallback": "inbound.req."
    }),
    "inbound.resp.": ("HEADER", None, False, None, True, {
        "reverse_context": "header_condition"
    }),
    "inbound.url.": ("CLIENT-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True, None),
    "now.": ("NOW", Validator.suffix_group(types.SuffixGroup.DATE_FIELDS), True, None, False, None),
    "outbound.conn.client-cert.SAN.":
        (
            "OUTBOUND:CLIENT-CERT:SAN",
            Validator.suffix_group(types.SuffixGroup.SAN_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.server-cert.SAN.":
        (
            "OUTBOUND:SERVER-CERT:SAN",
            Validator.suffix_group(types.SuffixGroup.SAN_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.client-cert.san.":
        (
            "OUTBOUND:CLIENT-CERT:SAN",
            Validator.suffix_group(types.SuffixGroup.SAN_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.server-cert.san.":
        (
            "OUTBOUND:SERVER-CERT:SAN",
            Validator.suffix_group(types.SuffixGroup.SAN_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.client-cert.":
        (
            "OUTBOUND:CLIENT-CERT",
            Validator.suffix_group(types.SuffixGroup.CERT_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.server-cert.":
        (
            "OUTBOUND:SERVER-CERT",
            Validator.suffix_group(types.SuffixGroup.CERT_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "outbound.conn.":
        (
            "OUTBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}, True, None),
    "outbound.cookie.":
        (
            "COOKIE", Validator.http_token(), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}, True, {
                "reverse_fallback": "inbound.cookie."
            }),
    "outbound.req.":
        (
            "HEADER", None, False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}, True, {
                "reverse_context": "header_condition"
            }),
    "outbound.resp.":
        (
            "HEADER",
            None,
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST},
            True,
            {
                "reverse_context": "header_condition"
            },
        ),
    "outbound.url.":
        (
            "NEXT-HOP",
            Validator.suffix_group(types.SuffixGroup.URL_FIELDS),
            True,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            True,
            None,
        ),
    "to.url.": ("TO-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True, None),
}

#
# Static Reverse Resolution Tables
#

# Fallback tag mappings for complex resolution
FALLBACK_TAG_MAP: dict[str, tuple[str, bool]] = {
    "HEADER": ("header_condition", True),
    "CLIENT-HEADER": ("inbound.req.", False),
    "COOKIE": ("inbound.cookie.", False),
    "INBOUND:CLIENT-CERT": ("inbound.conn.client-cert.", False),
    "INBOUND:SERVER-CERT": ("inbound.conn.server-cert.", False),
    "INBOUND:CLIENT-CERT:SAN": ("inbound.conn.client-cert.SAN.", False),
    "INBOUND:SERVER-CERT:SAN": ("inbound.conn.server-cert.SAN.", False),
    "OUTBOUND:CLIENT-CERT": ("outbound.conn.client-cert.", False),
    "OUTBOUND:SERVER-CERT": ("outbound.conn.server-cert.", False),
    "OUTBOUND:CLIENT-CERT:SAN": ("outbound.conn.client-cert.SAN.", False),
    "OUTBOUND:SERVER-CERT:SAN": ("outbound.conn.server-cert.SAN.", False)
}

# Context type to mapping name associations
CONTEXT_TYPE_MAP: dict[str, str | tuple[str, str]] = {
    "header_condition": ("HEADER_CONTEXT_MAP", "inbound.resp."),
    "header_ops": ("HEADER_CONTEXT_MAP", "inbound.resp."),
    "cookie_ops": "inbound.cookie.",
    "destination_ops": ("URL_CONTEXT_MAP", "inbound.url.")
}

# Operator command mappings for reverse resolution
OPERATOR_COMMAND_MAP: dict[str, tuple[str, str, Callable, Callable]] = {
    "set-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "rm-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "set-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "rm-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "set-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual),
    "rm-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual)
}

#
# Programmatically Generated Reverse Resolution Maps
#

# Generate reverse resolution map programmatically to eliminate duplication
REVERSE_RESOLUTION_MAP = get_complete_reverse_resolution_map()

#
# LSP Pattern Matching Tables
#


@dataclass(slots=True, frozen=True)
class PatternMatch:
    """Represents a matched pattern with context information."""
    pattern: str
    matched_part: str
    suffix: str
    context_type: str
    field_dict_key: str | None = None
    maps_to: str | None = None


class LSPPatternMatcher:
    """Table-driven pattern matcher for LSP hover functionality."""

    # Define pattern categories with their corresponding documentation field dictionaries
    FIELD_PATTERNS: Final[dict[str, tuple[str, str, str]]] = {
        'now.': ('TIME_FIELDS', 'Current Date/Time Field', 'NOW'),
        'id.': ('ID_FIELDS', 'Transaction/Process Identifier', 'ID'),
        'geo.': ('GEO_FIELDS', 'Geographic Information', 'GEO'),
    }

    # Header patterns with their context information
    HEADER_PATTERNS: Final[list[str]] = ['inbound.req.', 'inbound.resp.', 'outbound.req.', 'outbound.resp.']

    # Cookie patterns
    COOKIE_PATTERNS: Final[list[str]] = ['inbound.cookie.', 'outbound.cookie.']

    # Certificate patterns
    CERTIFICATE_PATTERNS: Final[tuple[str, ...]] = (
        'inbound.conn.client-cert.', 'inbound.conn.server-cert.', 'outbound.conn.client-cert.', 'outbound.conn.server-cert.')

    # Connection patterns
    CONNECTION_PATTERNS: Final[list[str]] = ['inbound.conn.', 'outbound.conn.']

    @classmethod
    def match_field_pattern(cls, expression: str) -> PatternMatch | None:
        """Match field patterns (now., id., geo.) against expression."""
        for pattern, (field_dict, context, tag) in cls.FIELD_PATTERNS.items():
            if expression.startswith(pattern):
                suffix = expression[len(pattern):]
                return PatternMatch(
                    pattern=pattern,
                    matched_part=expression,
                    suffix=suffix,
                    context_type=context,
                    field_dict_key=field_dict,
                    maps_to=f"%{{{tag}:{suffix.upper()}}}")
        return None

    @classmethod
    def match_header_pattern(cls, expression: str) -> PatternMatch | None:
        """Match header patterns against expression."""
        for pattern in cls.HEADER_PATTERNS:
            if expression.startswith(pattern):
                suffix = expression[len(pattern):]
                return PatternMatch(
                    pattern=pattern, matched_part=expression, suffix=suffix, context_type='Header', field_dict_key=None)
        return None

    @classmethod
    def match_cookie_pattern(cls, expression: str) -> PatternMatch | None:
        """Match cookie patterns against expression."""
        for pattern in cls.COOKIE_PATTERNS:
            if expression.startswith(pattern):
                suffix = expression[len(pattern):]
                return PatternMatch(
                    pattern=pattern, matched_part=expression, suffix=suffix, context_type='Cookie', field_dict_key=None)
        return None

    @classmethod
    def match_certificate_pattern(cls, expression: str) -> PatternMatch | None:
        """Match certificate patterns against expression."""
        for pattern in cls.CERTIFICATE_PATTERNS:
            if expression.startswith(pattern):
                suffix = expression[len(pattern):]
                return PatternMatch(
                    pattern=pattern, matched_part=expression, suffix=suffix, context_type='Certificate', field_dict_key=None)
        return None

    @classmethod
    def match_connection_pattern(cls, expression: str) -> PatternMatch | None:
        """Match connection patterns against expression."""
        for pattern in cls.CONNECTION_PATTERNS:
            if expression.startswith(pattern):
                suffix = expression[len(pattern):]
                return PatternMatch(
                    pattern=pattern,
                    matched_part=expression,
                    suffix=suffix,
                    context_type='Connection',
                    field_dict_key='CONN_FIELDS')
        return None

    @classmethod
    def match_any_pattern(cls, expression: str) -> PatternMatch | None:
        """Try to match expression against all pattern types."""
        # Try field patterns first (most specific)
        if match := cls.match_field_pattern(expression):
            return match

        # Try certificate patterns
        if match := cls.match_certificate_pattern(expression):
            return match

        # Try connection patterns
        if match := cls.match_connection_pattern(expression):
            return match

        # Try header patterns
        if match := cls.match_header_pattern(expression):
            return match

        # Try cookie patterns
        if match := cls.match_cookie_pattern(expression):
            return match

        return None
