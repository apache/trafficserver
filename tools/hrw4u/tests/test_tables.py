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
"""Tests for LSPPatternMatcher in tables.py."""
from __future__ import annotations

import pytest
from hrw4u.tables import LSPPatternMatcher


class TestMatchFieldPattern:

    def test_now_field(self):
        match = LSPPatternMatcher.match_field_pattern("now.YEAR")
        assert match is not None
        assert match.pattern == "now."
        assert match.suffix == "YEAR"
        assert match.context_type == "Current Date/Time Field"
        assert match.maps_to == "%{NOW:YEAR}"

    def test_id_field(self):
        match = LSPPatternMatcher.match_field_pattern("id.PROCESS")
        assert match is not None
        assert match.pattern == "id."
        assert match.field_dict_key == "ID_FIELDS"

    def test_geo_field(self):
        match = LSPPatternMatcher.match_field_pattern("geo.COUNTRY")
        assert match is not None
        assert match.pattern == "geo."
        assert match.maps_to == "%{GEO:COUNTRY}"

    def test_no_match(self):
        assert LSPPatternMatcher.match_field_pattern("inbound.req.X-Foo") is None


class TestMatchHeaderPattern:

    def test_inbound_req(self):
        match = LSPPatternMatcher.match_header_pattern("inbound.req.X-Foo")
        assert match is not None
        assert match.context_type == "Header"
        assert match.suffix == "X-Foo"

    def test_outbound_resp(self):
        match = LSPPatternMatcher.match_header_pattern("outbound.resp.Content-Type")
        assert match is not None
        assert match.suffix == "Content-Type"

    def test_no_match(self):
        assert LSPPatternMatcher.match_header_pattern("now.YEAR") is None


class TestMatchCookiePattern:

    def test_inbound_cookie(self):
        match = LSPPatternMatcher.match_cookie_pattern("inbound.cookie.session_id")
        assert match is not None
        assert match.context_type == "Cookie"
        assert match.suffix == "session_id"

    def test_outbound_cookie(self):
        match = LSPPatternMatcher.match_cookie_pattern("outbound.cookie.token")
        assert match is not None
        assert match.suffix == "token"

    def test_no_match(self):
        assert LSPPatternMatcher.match_cookie_pattern("inbound.req.X-Foo") is None


class TestMatchCertificatePattern:

    def test_inbound_client_cert(self):
        match = LSPPatternMatcher.match_certificate_pattern("inbound.conn.client-cert.CN")
        assert match is not None
        assert match.context_type == "Certificate"
        assert match.suffix == "CN"

    def test_outbound_server_cert(self):
        match = LSPPatternMatcher.match_certificate_pattern("outbound.conn.server-cert.SAN")
        assert match is not None

    def test_no_match(self):
        assert LSPPatternMatcher.match_certificate_pattern("inbound.req.X-Foo") is None


class TestMatchConnectionPattern:

    def test_inbound_conn(self):
        match = LSPPatternMatcher.match_connection_pattern("inbound.conn.TLS")
        assert match is not None
        assert match.context_type == "Connection"
        assert match.field_dict_key == "CONN_FIELDS"
        assert match.suffix == "TLS"

    def test_outbound_conn(self):
        match = LSPPatternMatcher.match_connection_pattern("outbound.conn.H2")
        assert match is not None

    def test_no_match(self):
        assert LSPPatternMatcher.match_connection_pattern("inbound.req.X-Foo") is None


class TestMatchAnyPattern:

    def test_field(self):
        match = LSPPatternMatcher.match_any_pattern("now.YEAR")
        assert match is not None
        assert match.context_type == "Current Date/Time Field"

    def test_header(self):
        match = LSPPatternMatcher.match_any_pattern("inbound.req.X-Foo")
        assert match is not None
        assert match.context_type == "Header"

    def test_cookie(self):
        match = LSPPatternMatcher.match_any_pattern("inbound.cookie.sid")
        assert match is not None
        assert match.context_type == "Cookie"

    def test_certificate(self):
        match = LSPPatternMatcher.match_any_pattern("inbound.conn.client-cert.CN")
        assert match is not None
        assert match.context_type == "Certificate"

    def test_connection(self):
        match = LSPPatternMatcher.match_any_pattern("inbound.conn.TLS")
        assert match is not None
        assert match.context_type == "Connection"

    def test_no_match(self):
        assert LSPPatternMatcher.match_any_pattern("completely.unknown.thing") is None


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
