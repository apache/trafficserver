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

import re

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Create the runroot whose environment locates traffic_cache_tool.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    return ats_factory.create("ts")


def test_traffic_cache_tool_bad_aos(ats_factory: ATSFactory) -> None:
    """A non-numeric --aos is reported by traffic_cache_tool, which exits with 1 instead of aborting.

    std::stoi throws std::invalid_argument on the value. main() must report the exception and
    return 1 rather than let it reach std::terminate, which would abort with SIGABRT.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    result = _ats.run("traffic_cache_tool", "--aos", "abc", "list")
    assert result.returncode == 1, result.output
    assert re.search(r"traffic_cache_tool: terminating on an unhandled std::\S*invalid_argument: \S", result.stderr), result.output
