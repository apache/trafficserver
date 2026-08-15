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
from typing import Any

from tools.uranium.services import ATS, ATSFactory
from uranium_tests.lib.jsonrpc import Request


class ReadOnlyRecordScenario:
    """Verify the management RPC refuses writes to RECA_READ_ONLY records."""

    RECORD = "proxy.config.thread.max_heartbeat_mseconds"
    DEFAULT_VALUE = "60"
    ATTEMPTED_VALUE = "999"
    RECA_READ_ONLY = "2"
    RECORD_READ_ONLY_CODE = 2009

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = ats_factory.create("ts")

    def request(self, request: object) -> dict[str, Any]:
        """Send one request and decode its JSON-RPC response."""

        result = self._ats.rpc(request)
        assert result.returncode == 0, result.output
        return json.loads(result.stdout)

    def lookup(self) -> dict[str, Any]:
        """Fetch the target record through admin_lookup_records."""

        response = self.request(Request.admin_lookup_records([{
            "record_name": self.RECORD,
            "rec_types": ["1"],
        }]))
        assert "error" not in response, response
        records = response["result"]["recordList"]
        assert len(records) == 1
        return records[0]["record"]

    def assert_default_read_only_value(self) -> None:
        """Verify the target record's value and access tier."""

        record = self.lookup()
        assert record["record_name"] == self.RECORD
        assert record["current_value"] == self.DEFAULT_VALUE
        assert str(record["config_meta"]["access_type"]) == self.RECA_READ_ONLY

    def run(self) -> None:
        """Attempt the rejected write and verify storage was unchanged."""

        self._ats.start()
        self.assert_default_read_only_value()
        response = self.request(
            Request.admin_config_set_records([{
                "record_name": self.RECORD,
                "record_value": self.ATTEMPTED_VALUE,
            }]))
        assert response["error"]["data"][0]["code"] == self.RECORD_READ_ONLY_CODE
        self.assert_default_read_only_value()


def test_traffic_ctl_set_read_only(ats_factory: ATSFactory) -> None:
    """The records RPC rejects a RECA_READ_ONLY write without changing data."""

    ReadOnlyRecordScenario(ats_factory).run()
