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

from tools.uranium.services import ATSFactory, ServiceFactory

from .sni_queue_scenario import run_rate_limit_sni


def test_rate_limit_sni_expiry(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Expired queued handshakes do not consume active capacity.

    :param ats_factory: Factory for the rate-limited ATS instance.
    :param services: Factory owning the concurrent handshake driver.
    """

    run_rate_limit_sni(
        ats_factory,
        services,
        queue_lines=("    queue:", "      size: 1", "      max_age: 1"),
        client_script="rate_limit_sni_expiry_client.sh",
        client_marker="rate_limit-expiry-done",
        traffic_marker="too old",
        failure_expression=r"Enabling queued VC|Rejecting connection|Releasing a slot, active entities == [0-9]{4,}|_active <= _limit|received signal",
    )
