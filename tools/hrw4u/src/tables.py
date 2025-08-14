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

from typing import Callable
from hrw4u.validation import Validator
import hrw4u.types as types
from hrw4u.states import SectionType
from hrw4u.common import MagicStrings, HeaderOperations

HEADER_OPERATIONS = HeaderOperations.OPERATIONS

OPERATOR_MAP: dict[str, tuple[str | list[str] | tuple[str, ...], Callable[[str], None] | None, bool, set[SectionType] | None]] = {
    "http.cntl.": ("set-http-cntl", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None),
    "http.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
    "http.status": ("set-status", Validator.range(0, 999), False, None),
    "inbound.conn.dscp": ("set-conn-dscp", Validator.nbit_int(6), False, None),
    "inbound.cookie.": (["rm-cookie", "set-cookie"], Validator.http_token(), False, None),
    "inbound.req.": (HEADER_OPERATIONS, Validator.http_token(), False, None),
    "inbound.resp.body": ("set-body", Validator.quoted_or_simple(), False, None),
    "inbound.resp.": (HEADER_OPERATIONS, Validator.http_token(), False, None),
    "inbound.status.reason": ("set-status-reason", Validator.quoted_or_simple(), False, None),
    "inbound.status": ("set-status", Validator.range(0, 999), False, None),
    "inbound.url.": (["rm-destination", "set-destination"], Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None),
    "outbound.cookie.": (["rm-cookie", "set-cookie"], Validator.http_token(), False, None),
    "outbound.req.":
        (HEADER_OPERATIONS, Validator.http_token(), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "outbound.resp.":
        (
            HEADER_OPERATIONS,
            Validator.http_token(),
            False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST},
        ),
    "outbound.status.reason":
        (
            "set-status-reason", Validator.quoted_or_simple(), False,
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
    "outbound.status":
        ("set-status", Validator.range(0, 999), False, {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST}),
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
    "skip-remap": ("skip-remap", Validator.arg_count(1).suffix_group(types.SuffixGroup.BOOL_FIELDS)),
}

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
            {SectionType.PRE_REMAP, SectionType.REMAP, SectionType.READ_REQUEST},
            False,
            {
                "reverse_tag": "STATUS",
                "ambiguous": True
            },
        ),
    "tcp.info": ("%{TCP-INFO}", None, False, None, False, None),

    # Prefix matches with reverse mapping info
    "capture.": ("LAST-CAPTURE", Validator.range(0, 9), False, None, True, None),
    "client.cert.": ("CLIENT-CERT", None, True, None, True, None),
    "from.url.": ("FROM-URL", Validator.suffix_group(types.SuffixGroup.URL_FIELDS), True, None, True, None),
    "geo.": ("GEO", Validator.suffix_group(types.SuffixGroup.GEO_FIELDS), True, None, True, None),
    "http.cntl.": ("HTTP-CNTL", Validator.suffix_group(types.SuffixGroup.HTTP_CNTL_FIELDS), True, None, False, None),
    "id.": ("ID", Validator.suffix_group(types.SuffixGroup.ID_FIELDS), True, None, False, None),
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
    "outbound.conn.": ("OUTBOUND", Validator.suffix_group(types.SuffixGroup.CONN_FIELDS), True, None, True, None),
    "outbound.cookie.": ("COOKIE", Validator.http_token(), False, None, True, {
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

# Reverse resolution map for inverse symbol resolution
REVERSE_RESOLUTION_MAP = {
    # IP payload mappings
    "IP": {
        "CLIENT": "inbound.ip",
        "INBOUND": "inbound.server",
        "SERVER": "outbound.ip",
        "OUTBOUND": "outbound.server",
    },
    # Ambiguous tag resolution with conditional logic
    "STATUS":
        {
            "outbound_sections":
                frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST, SectionType.READ_RESPONSE}),
            "outbound_result": "outbound.status",
            "inbound_result": "inbound.status",
        },
    "METHOD":
        {
            "outbound_sections": frozenset({SectionType.SEND_REQUEST}),
            "outbound_result": "outbound.method",
            "inbound_result": "inbound.method",
        },
    # Status target mappings
    "STATUS_TARGETS":
        {
            frozenset({SectionType.REMAP, SectionType.SEND_RESPONSE}): "inbound.status",
            frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST,
                       SectionType.READ_RESPONSE}): "outbound.status",
        },
    "HEADER_CONTEXT_MAP":
        {
            SectionType.REMAP: "inbound.req.",
            frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}): "outbound.req.",
            SectionType.READ_RESPONSE: "outbound.resp.",
        },
    "URL_CONTEXT_MAP":
        {
            SectionType.REMAP: "inbound.url.",
            frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}): "outbound.url.",
        },
    "CONTEXT_TYPE_MAP":
        {
            "header_condition": ("HEADER_CONTEXT_MAP", "inbound.resp."),
            "header_ops": ("HEADER_CONTEXT_MAP", "inbound.resp."),
            "cookie_ops": "inbound.cookie.",
            "destination_ops": ("URL_CONTEXT_MAP", "inbound.url."),
        },
    "FALLBACK_TAG_MAP":
        {
            "HEADER": ("header_condition", True),
            "CLIENT-HEADER": ("inbound.req.", False),
            "COOKIE": ("inbound.cookie.", False),
        },
}

# Pre-computed lookup optimizations for better performance
_OPERATOR_COMMAND_LOOKUP = {
    cmd: key for key, (commands, *_) in OPERATOR_MAP.items()
    for cmd in (commands if isinstance(commands, (list, tuple)) else [commands])
}

_CONDITION_TAG_LOOKUP = {
    tag.strip().removeprefix("%{").removesuffix("}").split(":", 1)[0]: key
    for key, (tag, *_) in CONDITION_MAP.items()
    if not key.endswith(".")
}

_FUNCTION_TAG_LOOKUP = {tag: func_name for func_name, (tag, _) in FUNCTION_MAP.items()}


def get_operator_key_for_command(command: str) -> str | None:
    """Fast lookup for operator key by command"""
    return _OPERATOR_COMMAND_LOOKUP.get(command)


def get_condition_key_for_tag(tag: str) -> str | None:
    """Fast lookup for condition key by tag"""
    return _CONDITION_TAG_LOOKUP.get(tag)


def get_function_name_for_tag(tag: str) -> str | None:
    """Fast lookup for function name by tag"""
    return _FUNCTION_TAG_LOOKUP.get(tag)


AMBIGUOUS_CONTEXT_TAGS = frozenset({"STATUS", "METHOD"})

OPERATOR_COMMAND_MAP = {
    "set-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "rm-header": ("header_ops", "header", lambda toks: toks[1], lambda qual: qual),
    "set-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "rm-cookie": ("cookie_ops", "cookie", lambda toks: toks[1], lambda qual: qual),
    "set-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual),
    "rm-destination": ("destination_ops", "destination", lambda toks: toks[1].lower(), lambda qual: qual),
}
