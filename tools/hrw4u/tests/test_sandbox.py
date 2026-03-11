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


@pytest.mark.sandbox
@pytest.mark.parametrize("input_file,error_file,sandbox_file", utils.collect_sandbox_deny_test_files("sandbox"))
def test_sandbox_denials(input_file: Path, error_file: Path, sandbox_file: Path) -> None:
    """Test that sandbox-denied features produce expected errors."""
    utils.run_sandbox_deny_test(input_file, error_file, sandbox_file)


@pytest.mark.sandbox
@pytest.mark.parametrize("input_file,output_file,sandbox_file", utils.collect_sandbox_allow_test_files("sandbox"))
def test_sandbox_allowed(input_file: Path, output_file: Path, sandbox_file: Path) -> None:
    """Test that features not in the deny list compile normally under a sandbox."""
    utils.run_sandbox_allow_test(input_file, output_file, sandbox_file)


@pytest.mark.sandbox
@pytest.mark.parametrize("input_file,warning_file,output_file,sandbox_file", utils.collect_sandbox_warn_test_files("sandbox"))
def test_sandbox_warnings(input_file: Path, warning_file: Path, output_file: Path, sandbox_file: Path) -> None:
    """Test that sandbox-warned features produce warnings but compile successfully."""
    utils.run_sandbox_warn_test(input_file, warning_file, output_file, sandbox_file)
