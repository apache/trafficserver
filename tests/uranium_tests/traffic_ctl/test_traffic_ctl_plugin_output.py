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
"""Plugin-list output preserves its JSON structure for empty and populated lists."""

import json
import pytest
from tools.uranium.services import ATS


@pytest.mark.parametrize("populated", [False, True], ids=["empty", "populated"])
def test_traffic_ctl_plugin_output(ats: ATS, populated: bool) -> None:
    """Check the complete payload and smoke-check the human-readable table.

    :param ats: Unstarted proxy.
    :param populated: Whether to load two global plugins.
    """
    if populated:
        for plugin in ("xdebug.so", "stats_over_http.so"):
            if not ats.plugin_exists(plugin):
                pytest.skip(f"{plugin} is required")
        ats.plugin_config.add_lines(["xdebug.so --enable=x-cache", "stats_over_http.so"])
    ats.records.update({"proxy.config.exec_thread.autoconfig.enabled": 0, "proxy.config.exec_thread.limit": 4})
    ats.start()
    result = ats.traffic_ctl("-f", "json", "plugin", "list")
    assert result.returncode == 0, result.output
    plugins = (
        [
            {"path": name, "enabled": "true", "status": "loaded", "index": str(index)}
            for index, name in enumerate(("xdebug.so", "stats_over_http.so"), 1)
        ]
        if populated
        else []
    )
    assert json.loads(result.stdout)["result"] == {"data": {"source": "plugin.config", "plugins": plugins}}
    result = ats.traffic_ctl("plugin", "list")
    assert result.returncode == 0, result.output
    for text in ["source: plugin.config", "xdebug.so", "stats_over_http.so", "loaded"] if populated else ["source: plugin.config"]:
        assert text in result.stdout
