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


@pytest.mark.procedures
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("procedures", "hrw4u"))
def test_output_matches(input_file: Path, output_file: Path) -> None:
    """Test that procedure expansion and hrw4u output matches expected output."""
    utils.run_procedure_output_test(input_file, output_file)


@pytest.mark.procedures
@pytest.mark.invalid
@pytest.mark.parametrize("input_file", utils.collect_failing_inputs("procedures"))
def test_invalid_inputs_fail(input_file: Path) -> None:
    """Test that invalid procedure inputs produce expected errors."""
    utils.run_procedure_failing_test(input_file)


@pytest.mark.procedures
@pytest.mark.parametrize("input_file,flatten_file", utils.collect_flatten_test_files("procedures"))
def test_flatten_matches(input_file: Path, flatten_file: Path) -> None:
    """Test that flatten mode produces expected self-contained hrw4u output."""
    utils.run_procedure_flatten_test(input_file, flatten_file)


@pytest.mark.procedures
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("procedures", "hrw4u"))
def test_flatten_roundtrip(input_file: Path, output_file: Path) -> None:
    """Test that flattened hrw4u compiles to the same header_rewrite as the original."""
    utils.run_procedure_flatten_roundtrip_test(input_file, output_file)
