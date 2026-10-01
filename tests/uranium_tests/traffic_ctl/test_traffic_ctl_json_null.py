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

import json

from tools.uranium.services import ATS


def test_traffic_ctl_json_null(ats: ATS) -> None:
    """Empty HostDB and plugin results are JSON arrays, not YAML nulls.

    :param ats: Fresh server with no requests or global plugins.
    """
    ats.records.update({"proxy.config.exec_thread.autoconfig.enabled": 0, "proxy.config.exec_thread.limit": 4})
    ats.start()
    for arguments in (("hostdb", "status"), ("hostdb", "status", "-f", "json"), ("server", "status")):
        result = ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        # ATS.run decodes strictly: replacement decoding could conceal
        # invalid JSON output by inserting a legal U+FFFD into a JSON string.
        document = json.loads(result.stdout)
        if arguments == ("hostdb", "status"):
            assert document["partitions"] == [], document
    for method, params in (("get_hostdb_status", {"hostname": ""}), ("admin_plugin_get_list", None), ("show_registered_handlers",
                                                                                                      None)):
        request = {"jsonrpc": "2.0", "id": "1", "method": method}
        if params is not None:
            request["params"] = params
        result = ats.rpc(request)
        assert result.returncode == 0, result.output
        document = json.loads(result.stdout)
        assert "error" not in document, document
        if method == "admin_plugin_get_list":
            assert document["result"] == {"data": {"source": "plugin.config", "plugins": []}}, document
