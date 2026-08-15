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

from cont_schedule_scenario import ContScheduleScenario
from tools.uranium.services import ATSFactory


@pytest.mark.parametrize(
    ("mode", "gold_name", "entire_pool_minimum"),
    [
        ("every_entire", "schedule_every_on_entire_pool.gold", 2),
        ("every_pool", "schedule_every_on_pool.gold", None),
        ("every_thread", "schedule_every_on_thread.gold", None),
        ("entire", "schedule_on_entire_pool.gold", 1),
        ("pool", "schedule_on_pool.gold", None),
        ("thread", "schedule_on_thread.gold", None),
        ("affinity", "thread_affinity.gold", None),
    ],
)
def test_cont_schedule(
    ats_factory: ATSFactory,
    mode: str,
    gold_name: str,
    entire_pool_minimum: int | None,
) -> None:
    """The continuation scheduling APIs run on their requested threads."""

    ContScheduleScenario(
        ats_factory,
        directory=Path(__file__).parent,
        mode=mode,
        gold_name=gold_name,
        entire_pool_minimum=entire_pool_minimum,
    ).run()
