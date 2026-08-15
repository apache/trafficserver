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
from uranium_tests.remap.remap_ws import RemapWebSocketScenario


def test_remap_ws(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Classic remap rules forward valid WebSocket upgrades."""

    RemapWebSocketScenario(ats_factory, services, curl, use_yaml=False).run()
