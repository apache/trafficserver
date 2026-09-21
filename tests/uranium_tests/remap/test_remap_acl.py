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

from pathlib import Path

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory
from uranium_tests.remap.all_acl_combinations import all_acl_combination_tests
from uranium_tests.remap.deactivate_ip_allow import all_deactivate_ip_allow_tests
from uranium_tests.remap.remap_acl import (
    AclCase,
    OldAclActionScenario,
    OldActionCase,
    RemapAclScenario,
    combination_acl_cases,
    old_action_cases,
    standard_acl_cases,
)

TEST_DIRECTORY = Path(__file__).parent
ACL_CASES = (
    standard_acl_cases(use_yaml=False) + combination_acl_cases(all_acl_combination_tests, prefix="combination") +
    combination_acl_cases(all_deactivate_ip_allow_tests, prefix="deactivate"))


@pytest.mark.parametrize("case", ACL_CASES, ids=lambda case: case.name)
def test_remap_acl(case: AclCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Classic remap ACL syntax produces the expected method decisions."""

    RemapAclScenario(ats_factory, services, case, use_yaml=False, test_directory=TEST_DIRECTORY).run()


@pytest.mark.parametrize("case", old_action_cases(use_yaml=False), ids=lambda case: case.name)
def test_remap_acl_rejects_old_actions(case: OldActionCase, ats_factory: ATSFactory) -> None:
    """Classic remap rejects obsolete actions under modern policy."""

    OldAclActionScenario(ats_factory, case, use_yaml=False).run()
