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

from hrw4u.ast_nodes import Target


class TestTarget:

    def test_dotted_path(self):
        t = Target.from_dotted("inbound.req.X-Foo")
        assert t.namespace == "inbound.req"
        assert t.field == "X-Foo"

    def test_two_segments(self):
        t = Target.from_dotted("inbound.ip")
        assert t.namespace == "inbound"
        assert t.field == "ip"

    def test_no_dots(self):
        t = Target.from_dotted("bool_0")
        assert t.namespace is None
        assert t.field == "bool_0"

    def test_deep_namespace(self):
        t = Target.from_dotted("http.cntl.TXN_DEBUG")
        assert t.namespace == "http.cntl"
        assert t.field == "TXN_DEBUG"
