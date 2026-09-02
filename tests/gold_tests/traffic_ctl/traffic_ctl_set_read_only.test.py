'''
Verify that the management RPC refuses to write records registered as
RECA_READ_ONLY.
'''
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

from jsonrpc import Request, Response

Test.Summary = '''
Verify that records registered with RECA_READ_ONLY cannot be modified through
the management RPC backing "traffic_ctl config set"
(admin_config_set_records).
'''

Test.ContinueOnFail = True

# proxy.config.thread.max_heartbeat_mseconds is registered as RECA_READ_ONLY
# in src/records/RecordsConfig.cc with a default of 60.  RECA_READ_ONLY = 2
# in the RecAccessT enum (see include/records/RecDefs.h).
READ_ONLY_RECORD = "proxy.config.thread.max_heartbeat_mseconds"
DEFAULT_VALUE = "60"
ATTEMPTED_VALUE = "999"
RECA_READ_ONLY = "2"
RECT_CONFIG_BIT = "1"  # bit value in the rec_types filter

# RecordError::RECORD_READ_ONLY in src/mgmt/rpc/handlers/common/RecordsUtils.h
# (assigned as Codes::RECORD + offset).  This is the per-tier code that the
# JSONRPC response must surface in error.data[*].code so programmatic
# clients can branch on the access tier without parsing the message text.
RECORD_READ_ONLY_CODE = 2009

ts = Test.MakeATSProcess("ts")


def lookup_request():
    """Build a JSONRPC request that fetches the read-only record."""
    return Request.admin_lookup_records([{"record_name": READ_ONLY_RECORD, "rec_types": [RECT_CONFIG_BIT]}])


def assert_record_at_default(resp: Response):
    """Validate the looked-up record is RECA_READ_ONLY and at its default value."""
    if resp.is_error():
        return (False, f"unexpected error: {resp.error_as_str()}")

    records = resp.result.get('recordList', [])
    if len(records) != 1:
        return (False, f"expected exactly 1 record, got {len(records)}")

    rec = records[0]['record']
    if rec.get('record_name') != READ_ONLY_RECORD:
        return (False, f"record_name {rec.get('record_name')!r} != {READ_ONLY_RECORD!r}")
    if rec.get('current_value') != DEFAULT_VALUE:
        return (False, f"current_value {rec.get('current_value')!r} != {DEFAULT_VALUE!r}")

    access = rec.get('config_meta', {}).get('access_type')
    if str(access) != RECA_READ_ONLY:
        return (False, f"access_type {access!r} != {RECA_READ_ONLY!r} (record is not RECA_READ_ONLY)")

    return (True, "record is RECA_READ_ONLY and at the registered default")


def assert_set_was_rejected(resp: Response):
    """Validate the set attempt produced the per-tier not-writable error code."""
    if not resp.is_error():
        return (False, f"set should have failed but returned a result: {resp.result!r}")
    # Validate the structured error code rather than the message text so the
    # test stays meaningful if the error wording is ever rephrased and so
    # that any regression to the generic Codes::RECORD (2000) is caught.
    if not resp.contains_nested_error(code=RECORD_READ_ONLY_CODE):
        return (False, f"expected nested error code {RECORD_READ_ONLY_CODE} (RECORD_READ_ONLY); got: {resp.error_as_str()}")
    return (True, f"set was refused with code {RECORD_READ_ONLY_CODE} (RECORD_READ_ONLY)")


# Step 0: confirm the record is registered as RECA_READ_ONLY and starts at
# its registered default value.  This anchors the rest of the test against
# the registration in RecordsConfig.cc -- if someone reclassifies the record
# the test fails noisily here instead of silently exercising a permissive
# write path.
tr = Test.AddTestRun("Confirm record is RECA_READ_ONLY and at default")
tr.Processes.Default.StartBefore(ts)
tr.AddJsonRPCClientRequest(ts, lookup_request())
tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(assert_record_at_default)

# Step 1: attempt to write the record via the management RPC.  The handler
# must reject the request with the "Record is not writable" error.
tr = Test.AddTestRun("Attempt to set the RECA_READ_ONLY record (must be refused)")
tr.AddJsonRPCClientRequest(
    ts, Request.admin_config_set_records([{
        "record_name": READ_ONLY_RECORD,
        "record_value": ATTEMPTED_VALUE,
    }]))
tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(assert_set_was_rejected)

# Step 2: re-look-up the record.  Even if step 1's response had been
# misleading, the record must still hold its default value -- this is the
# assertion that catches the underlying bug at the storage level.
tr = Test.AddTestRun("Confirm record was not modified")
tr.AddJsonRPCClientRequest(ts, lookup_request())
tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(assert_record_at_default)
