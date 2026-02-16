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

import pytest
import utils


@pytest.mark.conds
def test_conds_bulk_compilation() -> None:
    """Test bulk compilation of all conds test cases."""
    utils.run_bulk_test("conds")


@pytest.mark.examples
def test_examples_bulk_compilation() -> None:
    """Test bulk compilation of all examples test cases."""
    utils.run_bulk_test("examples")


@pytest.mark.hooks
def test_hooks_bulk_compilation() -> None:
    """Test bulk compilation of all hooks test cases."""
    utils.run_bulk_test("hooks")


@pytest.mark.ops
def test_ops_bulk_compilation() -> None:
    """Test bulk compilation of all ops test cases."""
    utils.run_bulk_test("ops")


@pytest.mark.vars
def test_vars_bulk_compilation() -> None:
    """Test bulk compilation of all vars test cases."""
    utils.run_bulk_test("vars")
