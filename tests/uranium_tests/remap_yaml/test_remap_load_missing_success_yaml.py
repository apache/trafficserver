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

from remap_load import RemapLoadScenario
from tools.uranium.services import ATSFactory


def test_remap_load_missing_success_yaml(ats_factory: ATSFactory) -> None:
    """A missing remap.yaml is accepted when no rule is required."""

    RemapLoadScenario(ats_factory, use_yaml=True, file_exists=False, should_start=True).run()
