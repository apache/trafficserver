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

READ_ONLY_RECORD_RECORD_READ_ONLY_CODE = 2009
READ_ONLY_RECORD_RECA_READ_ONLY = "2"
READ_ONLY_RECORD_ATTEMPTED_VALUE = "999"
READ_ONLY_RECORD_DEFAULT_VALUE = "60"

READ_ONLY_RECORD_RECORD = "proxy.config.thread.max_heartbeat_mseconds"


def _request(request: object, *, _ats: ATS) -> dict[str, Any]:
    """Send one request and decode its JSON-RPC response.

    :param _ats: Test-local ats configured by the test.
    :param request: Request used by this test step.
    """

    result = _ats.rpc(request)
    assert result.returncode == 0, result.output
    return json.loads(result.stdout)


def lookup(*, _ats: ATS) -> dict[str, Any]:
    """Fetch the target record through admin_lookup_records.

    :param _ats: Test-local ats configured by the test.
    """

    response = _request(
        Request.admin_lookup_records([{
            "record_name": READ_ONLY_RECORD_RECORD,
            "rec_types": ["1"],
        }]), _ats=_ats)
    assert "error" not in response, response
    records = response["result"]["recordList"]
    assert len(records) == 1
    return records[0]["record"]


def assert_default_read_only_value(*, _ats: ATS) -> None:
    """Verify the target record's value and access tier.

    :param _ats: Test-local ats configured by the test.
    """

    record = lookup(_ats=_ats)
    assert record["record_name"] == READ_ONLY_RECORD_RECORD
    assert record["current_value"] == READ_ONLY_RECORD_DEFAULT_VALUE
    assert str(record["config_meta"]["access_type"]) == READ_ONLY_RECORD_RECA_READ_ONLY


def test_traffic_ctl_set_read_only(ats_factory: ATSFactory) -> None:
    """The records RPC rejects a RECA_READ_ONLY write without changing data.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = ats_factory.create("ts")

    _ats.start()
    assert_default_read_only_value(_ats=_ats)
    response = _request(
        Request.admin_config_set_records(
            [{
                "record_name": READ_ONLY_RECORD_RECORD,
                "record_value": READ_ONLY_RECORD_ATTEMPTED_VALUE,
            }]),
        _ats=_ats)
    assert response["error"]["data"][0]["code"] == READ_ONLY_RECORD_RECORD_READ_ONLY_CODE
    assert_default_read_only_value(_ats=_ats)
