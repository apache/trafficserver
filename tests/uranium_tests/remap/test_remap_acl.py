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
"""Retain all three release-branch ACL tables and the PROXY address case."""
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory
from uranium_tests.remap.all_acl_combinations import all_acl_combination_tests
from uranium_tests.remap.deactivate_ip_allow import all_deactivate_ip_allow_tests
from uranium_tests.remap.named_filter_combinations import named_filter_combination_tests
from uranium_tests.remap.remap_acl import AclCase, combination_acl_cases, run_remap_acl

ACL_CASES = [
    AclCase(
        "proxy-protocol",
        "ip_allow:\n  - apply: in\n    ip_addrs: 0/0\n    action: allow\n    methods: [GET]\n",
        "@action=allow @src_ip=1.2.3.4 @method=GET @method=POST",
        expected_responses=(200, 200, 403, 403, 403),
        deactivate_ip_allow=True,
        proxy_protocol=True),
    *combination_acl_cases(all_acl_combination_tests, prefix="combination"),
    *combination_acl_cases(all_deactivate_ip_allow_tests, prefix="deactivate"),
    *combination_acl_cases(named_filter_combination_tests, prefix="named"),
]


@pytest.mark.parametrize("case", ACL_CASES, ids=lambda case: case.name)
def test_remap_acl(case: AclCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Apply 9.2 allow/deny semantics, including multiple activated filters.

    :param case: Original release-branch ACL expectation.
    :param ats_factory: Independent proxy factory.
    :param services: Verifier service owner.
    """
    run_remap_acl(ats_factory, services, case, test_directory=Path(__file__).parent)
