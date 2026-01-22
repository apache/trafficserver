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
from typing import Final, Callable
from dataclasses import dataclass
from hrw4u.generators import get_complete_reverse_resolution_map
from hrw4u.validation import Validator
from hrw4u.types import MapParams, SuffixGroup
from hrw4u.states import SectionType
from hrw4u.common import HeaderOperations

# Common section sets for validation
# HTTP_SECTIONS: All hooks where HTTP transaction data is available (excludes TXN_START/TXN_CLOSE)
HTTP_SECTIONS: Final[frozenset[SectionType]] = frozenset(
    {
        SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST, SectionType.READ_RESPONSE,
        SectionType.SEND_RESPONSE
    })

# yapf: disable
OPERATOR_MAP: dict[str, MapParams] = {
    "http.cntl.": MapParams(target="set-http-cntl", upper=True, validate=Validator.suffix_group(SuffixGroup.HTTP_CNTL_FIELDS), sections=HTTP_SECTIONS),
    "http.status.reason": MapParams(target="set-status-reason", validate=Validator.quoted_or_simple(), sections=HTTP_SECTIONS),
    "http.status": MapParams(target="set-status", validate=Validator.range(0, 999), sections=HTTP_SECTIONS),
    "inbound.conn.dscp": MapParams(target="set-conn-dscp", validate=Validator.nbit_int(6), sections=HTTP_SECTIONS),
    "inbound.conn.mark": MapParams(target="set-conn-mark", validate=Validator.nbit_int(32), sections=HTTP_SECTIONS),
    "outbound.conn.dscp": MapParams(target="set-conn-dscp", validate=Validator.nbit_int(6), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.mark": MapParams(target="set-conn-mark", validate=Validator.nbit_int(32), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "inbound.cookie.": MapParams(target=HeaderOperations.COOKIE_OPERATIONS, validate=Validator.http_token(), sections=HTTP_SECTIONS),
    "inbound.req.": MapParams(target=HeaderOperations.OPERATIONS, add=True, validate=Validator.http_header_name(), sections=HTTP_SECTIONS),
    "inbound.resp.body": MapParams(target="set-body", validate=Validator.quoted_or_simple(), sections=HTTP_SECTIONS),
    "inbound.resp.": MapParams(target=HeaderOperations.OPERATIONS, add=True, validate=Validator.http_header_name(), sections={SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE, SectionType.TXN_CLOSE}),
    "inbound.status.reason": MapParams(target="set-status-reason", validate=Validator.quoted_or_simple(), sections=HTTP_SECTIONS),
    "inbound.status": MapParams(target="set-status", validate=Validator.range(0, 999), sections=HTTP_SECTIONS),
    "inbound.url.": MapParams(target=HeaderOperations.DESTINATION_OPERATIONS, upper=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections=HTTP_SECTIONS),
    "outbound.cookie.": MapParams(target=HeaderOperations.COOKIE_OPERATIONS, validate=Validator.http_token(), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.req.": MapParams(target=HeaderOperations.OPERATIONS, add=True, validate=Validator.http_header_name(), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.resp.": MapParams(target=HeaderOperations.OPERATIONS, add=True, validate=Validator.http_header_name(), sections={SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.status.reason": MapParams(target="set-status-reason", validate=Validator.quoted_or_simple(), sections=HTTP_SECTIONS),
    "outbound.status": MapParams(target="set-status", validate=Validator.range(0, 999), sections=HTTP_SECTIONS),
    "outbound.url.": MapParams(target=HeaderOperations.DESTINATION_OPERATIONS, upper=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections={SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST})
}

STATEMENT_FUNCTION_MAP: dict[str, MapParams] = {
    "add-header": MapParams(target="add-header", validate=Validator.arg_count(2).arg_at(0, Validator.http_header_name()).arg_at(1, Validator.quoted_or_simple()), sections=HTTP_SECTIONS),
    "counter": MapParams(target="counter", validate=Validator.arg_count(1).quoted_or_simple()),
    "set-debug": MapParams(target="set-debug", validate=Validator.arg_count(0)),
    "no-op": MapParams(target="no-op", validate=Validator.arg_count(0)),
    "remove_query": MapParams(target="rm-destination QUERY", validate=Validator.arg_count(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "keep_query": MapParams(target="rm-destination QUERY", validate=Validator.arg_count(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "run-plugin": MapParams(target="run-plugin", validate=Validator.min_args(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "set-body-from": MapParams(target="set-body-from", validate=Validator.arg_count(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "set-cc-alg": MapParams(target="set-cc-alg", validate=Validator.arg_count(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "set-config": MapParams(target="set-config", validate=Validator.arg_count(2).quoted_or_simple(), sections=HTTP_SECTIONS),
    "set-effective-address": MapParams(target="set-effective-address", validate=Validator.arg_count(1).quoted_or_simple(), sections=HTTP_SECTIONS),
    "set-redirect": MapParams(target="set-redirect", validate=Validator.arg_count(2).arg_at(0, Validator.range(300, 399)).arg_at(1, Validator.quoted_or_simple()), sections=HTTP_SECTIONS),
    "skip-remap": MapParams(target="skip-remap", validate=Validator.arg_count(1).suffix_group(SuffixGroup.BOOL_FIELDS)._add(Validator.normalize_arg_at(0)), sections={SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "set-plugin-cntl": MapParams(target="set-plugin-cntl", validate=Validator.arg_count(2)._add(Validator.normalize_arg_at(0)).arg_at(0, Validator.suffix_group(SuffixGroup.PLUGIN_CNTL_FIELDS))._add(Validator.normalize_arg_at(1))._add(Validator.conditional_arg_validation(SuffixGroup.PLUGIN_CNTL_MAPPING.value)), sections=HTTP_SECTIONS),
}

FUNCTION_MAP: dict[str, MapParams] = {
    "access": MapParams(target="ACCESS", validate=Validator.arg_count(1).quoted_or_simple()),
    "cache": MapParams(target="CACHE", validate=Validator.arg_count(0)),
    "cidr": MapParams(target="CIDR", validate=Validator.arg_count(2).arg_at(0, Validator.range(1, 32)).arg_at(1, Validator.range(1, 128))),
    "internal": MapParams(target="INTERNAL-TRANSACTION", validate=Validator.arg_count(0)),
    "random": MapParams(target="RANDOM", validate=Validator.arg_count(1).nbit_int(32)),
    "ssn-txn-count": MapParams(target="SSN-TXN-COUNT", validate=Validator.arg_count(0)),
    "txn-count": MapParams(target="TXN-COUNT", validate=Validator.arg_count(0)),
}

CONDITION_MAP: dict[str, MapParams] = {
    # Exact matches with reverse mapping info
    "inbound.ip": MapParams(target="%{IP:CLIENT}", rev={"reverse_tag": "IP", "reverse_payload": "CLIENT"}),
    "inbound.method": MapParams(target="%{METHOD}", sections=HTTP_SECTIONS, rev={"reverse_tag": "METHOD", "ambiguous": True}),
    "inbound.server": MapParams(target="%{IP:INBOUND}", rev={"reverse_tag": "IP", "reverse_payload": "INBOUND"}),
    "inbound.status": MapParams(target="%{STATUS}", sections=HTTP_SECTIONS, rev={"reverse_tag": "STATUS", "ambiguous": True}),
    "now": MapParams(target="%{NOW}"),
    "outbound.ip": MapParams(target="%{IP:SERVER}", sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_tag": "IP", "reverse_payload": "SERVER"}),
    "outbound.method": MapParams(target="%{METHOD}", sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_tag": "METHOD", "ambiguous": True}),
    "outbound.server": MapParams(target="%{IP:OUTBOUND}", sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_tag": "IP", "reverse_payload": "OUTBOUND"}),
    "outbound.status": MapParams(target="%{STATUS}", sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_tag": "STATUS", "ambiguous": True}),
    "tcp.info": MapParams(target="%{TCP-INFO}"),

    # Prefix matches
    "capture.": MapParams(target="LAST-CAPTURE", prefix=True, validate=Validator.range(0, 9)),
    "from.url.query.": MapParams(target="FROM-URL:QUERY", prefix=True, validate=Validator.http_token(), sections=HTTP_SECTIONS),
    "from.url.": MapParams(target="FROM-URL", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections=HTTP_SECTIONS),
    "geo.": MapParams(target="GEO", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.GEO_FIELDS)),
    "http.cntl.": MapParams(target="HTTP-CNTL", upper=True, validate=Validator.suffix_group(SuffixGroup.HTTP_CNTL_FIELDS), sections=HTTP_SECTIONS),
    "id.": MapParams(target="ID", upper=True, validate=Validator.suffix_group(SuffixGroup.ID_FIELDS)),
    "inbound.conn.client-cert.SAN.": MapParams(target="INBOUND:CLIENT-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS)),
    "inbound.conn.server-cert.SAN.": MapParams(target="INBOUND:SERVER-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS)),
    "inbound.conn.client-cert.san.": MapParams(target="INBOUND:CLIENT-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS)),
    "inbound.conn.server-cert.san.": MapParams(target="INBOUND:SERVER-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS)),
    "inbound.conn.client-cert.": MapParams(target="INBOUND:CLIENT-CERT", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CERT_FIELDS)),
    "inbound.conn.server-cert.": MapParams(target="INBOUND:SERVER-CERT", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CERT_FIELDS)),
    "inbound.conn.": MapParams(target="INBOUND", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CONN_FIELDS)),
    "inbound.cookie.": MapParams(target="COOKIE", prefix=True, validate=Validator.http_token(), sections=HTTP_SECTIONS, rev={"reverse_fallback": "inbound.cookie."}),
    "inbound.req.": MapParams(target="CLIENT-HEADER", prefix=True, validate=Validator.http_header_name(), sections=HTTP_SECTIONS, rev={"reverse_fallback": "inbound.req."}),
    "inbound.resp.": MapParams(target="HEADER", prefix=True, validate=Validator.http_header_name(), sections={SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE, SectionType.TXN_CLOSE}, rev={"reverse_context": "header_condition"}),
    "inbound.url.query.": MapParams(target="CLIENT-URL:QUERY", prefix=True, validate=Validator.http_token(), sections=HTTP_SECTIONS),
    "inbound.url.": MapParams(target="CLIENT-URL", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections=HTTP_SECTIONS),
    "now.": MapParams(target="NOW", upper=True, validate=Validator.suffix_group(SuffixGroup.DATE_FIELDS)),
    "outbound.conn.client-cert.SAN.": MapParams(target="OUTBOUND:CLIENT-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.server-cert.SAN.": MapParams(target="OUTBOUND:SERVER-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.client-cert.san.": MapParams(target="OUTBOUND:CLIENT-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.server-cert.san.": MapParams(target="OUTBOUND:SERVER-CERT:SAN", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.SAN_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.client-cert.": MapParams(target="OUTBOUND:CLIENT-CERT", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CERT_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.server-cert.": MapParams(target="OUTBOUND:SERVER-CERT", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CERT_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.conn.": MapParams(target="OUTBOUND", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.CONN_FIELDS), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.cookie.": MapParams(target="COOKIE", prefix=True, validate=Validator.http_token(), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_fallback": "inbound.cookie."}),
    "outbound.req.": MapParams(target="HEADER", prefix=True, validate=Validator.http_header_name(), sections={SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_context": "header_condition"}),
    "outbound.resp.": MapParams(target="HEADER", prefix=True, validate=Validator.http_header_name(), sections={SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}, rev={"reverse_context": "header_condition"}),
    "outbound.url.query.": MapParams(target="NEXT-HOP:QUERY", prefix=True, validate=Validator.http_token(), sections={SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "outbound.url.": MapParams(target="NEXT-HOP", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections={SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST, SectionType.READ_RESPONSE, SectionType.SEND_RESPONSE}),
    "to.url.query.": MapParams(target="TO-URL:QUERY", prefix=True, validate=Validator.http_token(), sections=HTTP_SECTIONS),
    "to.url.": MapParams(target="TO-URL", upper=True, prefix=True, validate=Validator.suffix_group(SuffixGroup.URL_FIELDS), sections=HTTP_SECTIONS),
}

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
    "OUTBOUND:SERVER-CERT:SAN": ("outbound.conn.server-cert.SAN.", False),
    "CLIENT-URL:QUERY": ("inbound.url.query.", False),
    "NEXT-HOP:QUERY": ("outbound.url.query.", False),
    "FROM-URL:QUERY": ("from.url.query.", False),
    "TO-URL:QUERY": ("to.url.query.", False)
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
    "add-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "set-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "rm-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "set-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "rm-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "set-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual),
    "rm-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual)
}
# yapf: enable

REVERSE_RESOLUTION_MAP = get_complete_reverse_resolution_map()


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

    FIELD_PATTERNS: Final[dict[str, tuple[str, str, str]]] = {
        'now.': ('TIME_FIELDS', 'Current Date/Time Field', 'NOW'),
        'id.': ('ID_FIELDS', 'Transaction/Process Identifier', 'ID'),
        'geo.': ('GEO_FIELDS', 'Geographic Information', 'GEO'),
    }

    HEADER_PATTERNS: Final[list[str]] = ['inbound.req.', 'inbound.resp.', 'outbound.req.', 'outbound.resp.']
    COOKIE_PATTERNS: Final[list[str]] = ['inbound.cookie.', 'outbound.cookie.']
    CERTIFICATE_PATTERNS: Final[tuple[str, ...]] = (
        'inbound.conn.client-cert.', 'inbound.conn.server-cert.', 'outbound.conn.client-cert.', 'outbound.conn.server-cert.')
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

        if match := cls.match_field_pattern(expression):
            return match

        if match := cls.match_certificate_pattern(expression):
            return match

        if match := cls.match_connection_pattern(expression):
            return match

        if match := cls.match_header_pattern(expression):
            return match

        if match := cls.match_cookie_pattern(expression):
            return match

        return None
