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

import pytest

from hrw4u.ast_nodes import Span


class TestSpan:

    def test_equality_is_by_value(self):
        assert Span(file="a", line=1, column=0) == Span(file="a", line=1, column=0)
        assert Span(file="a", line=1, column=0) != Span(file="b", line=1, column=0)

    def test_is_hashable_so_it_can_key_a_span_index(self):
        first = Span(file="a", line=1, column=0)
        assert {first: "node"}[Span(file="a", line=1, column=0)] == "node"

    def test_is_immutable(self):
        span = Span(file="a", line=1, column=0)
        with pytest.raises(Exception):
            span.line = 2

    def test_rejects_unknown_attributes(self):
        # slots=True: a typo'd field must fail loudly rather than sit unread on the instance.
        with pytest.raises(AttributeError):
            Span(file="a", line=1, column=0).lineno = 2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
