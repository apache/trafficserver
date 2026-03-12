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
"""
Debug-mode tests: re-run the examples group with debug=True.

The examples group exercises the most diverse visitor code paths
(conditions, operators, hooks, vars) and is sufficient to reach
100% coverage of the Dbg class. Running all groups in debug mode
is redundant since debug tracing doesn't affect output correctness.
"""
from __future__ import annotations

from pathlib import Path

import pytest
import utils


@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("examples", "hrw4u"))
def test_examples_debug(input_file: Path, output_file: Path) -> None:
    """Test hrw4u examples output matches with debug enabled."""
    utils.run_output_test(input_file, output_file, debug=True)


@pytest.mark.reverse
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("examples", "u4wrh"))
def test_examples_reverse_debug(input_file: Path, output_file: Path) -> None:
    """Test u4wrh examples reverse conversion with debug enabled."""
    utils.run_reverse_test(input_file, output_file, debug=True)
