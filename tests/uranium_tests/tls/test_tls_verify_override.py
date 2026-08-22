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

from tools.uranium.services import ATSFactory, Curl, ServiceFactory

from uranium_tests.tls.tls_verify_override_scenario import TlsVerifyOverrideScenario


def test_tls_verify_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Per-remap verification and remap/host SNI policies override global defaults."""

    TlsVerifyOverrideScenario(ats_factory, services, curl, include_server_name=False).run()
