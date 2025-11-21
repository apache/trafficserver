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
@pytest.mark.reverse
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("hooks", "u4wrh"))
def test_reverse_conversion(input_file: Path, output_file: Path) -> None:
    """Test that u4wrh reverse conversion produces original hrw4u for hooks test cases."""
    utils.run_reverse_test(input_file, output_file)
