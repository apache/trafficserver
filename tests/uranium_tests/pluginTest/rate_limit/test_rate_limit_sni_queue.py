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

from .sni_queue_scenario import RateLimitSniScenario


def test_rate_limit_sni_queue(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Closing a queued SNI handshake does not underflow the active-slot counter."""

    RateLimitSniScenario(
        ats_factory,
        services,
        queue_lines=("    queue:", "      size: 1"),
        client_script="rate_limit_sni_queue_client.sh",
        client_marker="rate_limit-queue-crash-done",
        traffic_marker="Queueing the VC",
        failure_expression=r"_active <= _limit|received signal",
    ).run()
