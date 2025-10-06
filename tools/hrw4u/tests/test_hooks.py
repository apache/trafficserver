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

from pathlib import Path

import pytest
import utils


@pytest.mark.hooks
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("hooks", "hrw4u"))
def test_output_matches(input_file: Path, output_file: Path) -> None:
    """Test that hrw4u output matches expected output for hooks test cases."""
    utils.run_output_test(input_file, output_file)


@pytest.mark.hooks
@pytest.mark.ast
@pytest.mark.parametrize("input_file,ast_file", utils.collect_ast_test_files("hooks"))
def test_ast_matches(input_file: Path, ast_file: Path) -> None:
    """Test that AST structure matches expected AST for hooks test cases."""
    utils.run_ast_test(input_file, ast_file)


@pytest.mark.hooks
@pytest.mark.invalid
@pytest.mark.parametrize("input_file", utils.collect_failing_inputs("hooks"))
def test_invalid_inputs_fail(input_file: Path) -> None:
    """Test that invalid hooks inputs produce expected errors."""
    utils.run_failing_test(input_file)
