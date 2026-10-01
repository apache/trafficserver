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

import pytest

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure classic remap syntax with the float override file.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("conf_remap.so"):
        pytest.skip("conf_remap.so is required")
    ats.write_config_file(
        "conf_remap.yaml",
        "records:\n  http:\n    background_fill_completed_threshold: !!float '0.5'\n",
    )
    ats.remap_config.add_line(
        "map http://cdn.example.com/ http://origin.example.com/ "
        f"@plugin=conf_remap.so @pparam={ats.config_directory}/conf_remap.yaml")
    return ats


def test_conf_remap_float(ats_factory: ATSFactory) -> None:
    """conf_remap accepts a YAML float record with classic remap syntax.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    result = _ats.traffic_ctl("config", "describe", "proxy.config.http.background_fill_completed_threshold")
    assert result.returncode == 0, result.output
