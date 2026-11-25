"""Verify that removing a range from the per-client connection exempt list with the Lua plugin un-exempts the addresses
it covers, so that proxy.config.net.per_client.max_connections_in applies to them again."""
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

from collections.abc import Iterable, Mapping
import time

import pytest

from tools.uranium.services import ATS, ATSFactory

from .throttle_client import attempt, hold

LIMIT = 3
OPEN = "proxy.process.net.connections_currently_open"
THROTTLED = "proxy.process.net.per_client.connections_throttled_in"
EXEMPT = "proxy.process.net.per_client.connections_exempt_in"

# Each case: the exempt list calls the Lua script makes at load, whether a connection from 127.0.0.1 past the limit is
# then served or refused, and whether the case needs the libswoc fix. Only the remove-covering-* cases fail without the
# fix, because only they remove a range that starts below every entry and covers one. The rest are controls.
CASES = [
    pytest.param([], "refused", False, id="no-list"),
    pytest.param([("add", "127.0.0.1")], "served", False, id="add-only"),
    pytest.param([("add", "127.0.0.1"), ("remove", "127.0.0.1")], "refused", False, id="remove-exact"),
    pytest.param([("add", "127.0.0.1"), ("remove", "127.0.0.0/8")], "refused", True, id="remove-covering-net"),
    pytest.param([("add", "127.0.0.1"), ("remove", "127.0.0.0-127.0.0.255")], "refused", True, id="remove-covering-range"),
    # An entry below the removed range, so the erase starts from an existing node.
    pytest.param(
        [("add", "10.0.0.1"), ("add", "127.0.0.1"), ("remove", "127.0.0.0/8")], "refused", False, id="remove-net-with-lower-entry"
    ),
    # Ranges below every entry that do not cover 127.0.0.1, which must stay exempt.
    pytest.param([("add", "127.0.0.1"), ("remove", "10.0.0.0/8")], "served", False, id="remove-below-keeps"),
    pytest.param([("add", "127.0.0.1"), ("remove", "127.0.0.0")], "served", False, id="remove-adjacent-below-keeps"),
]


def lua_script(calls: list[tuple[str, str]]) -> str:
    """Return a global Lua script that makes @a calls to the exempt list API at load and fails the load if any fails."""

    lines = []
    for op, ranges in calls:
        lines.append(f"assert(ts.connection_limit_exempt_list_{op}('{ranges}'), 'exempt_list_{op}({ranges}) failed')")
    return "\n".join(lines) + "\n"


def metrics(ats: ATS, names: Iterable[str]) -> dict[str, int]:
    """Return the values of the metrics @a names, read in one traffic_ctl call."""

    result = ats.traffic_ctl("metric", "get", *names)
    assert result.returncode == 0, result.output
    return {name: int(value) for name, value in (line.split() for line in result.stdout.splitlines())}


def wait_for_metrics(ats: ATS, expected: Mapping[str, int], timeout: float = 10) -> dict[str, int]:
    """Wait until one read of the metrics shows each metric in @a expected at its value, and return that read."""

    deadline = time.monotonic() + timeout
    values = metrics(ats, expected)
    while values != expected and time.monotonic() < deadline:
        time.sleep(0.1)
        values = metrics(ats, expected)
    assert values == expected, f"Expected the metrics {expected}, found {values}"
    return values


@pytest.mark.parametrize(("calls", "expected", "needs_fix"), CASES)
def test_exempt_list_remove(ats_factory: ATSFactory, calls: list[tuple[str, str]], expected: str, needs_fix: bool) -> None:
    """Fill the per-client limit from 127.0.0.1, then check whether one more connection is served."""

    # The fix is in lib/swoc, which a build against a system libswoc does not use.
    if needs_fix and ats_factory.has_feature("TS_USE_EXTERNAL_LIBSWOC"):
        pytest.skip("built against an external libswoc, which may not have the DiscreteSpace::erase fix")
    ats = ats_factory.create("ts")
    for plugin in ("statichit.so", "tslua.so"):
        if not ats.plugin_exists(plugin):
            pytest.skip(f"{plugin} is required")
    ats.write_config_file("healthcheck.txt", "OK\n")
    ats.remap_config.add_line(
        f"map / http://127.0.0.1/ @plugin=statichit.so @pparam=--file-path={ats.config_directory / 'healthcheck.txt'}"
    )
    if calls:
        ats.write_config_file("exempt.lua", lua_script(calls))
        ats.plugin_config.add_line(f"tslua.so --states=1 {ats.config_directory / 'exempt.lua'}")
    ats.records.update(
        {
            "proxy.config.http.server_ports": f"{ats.http_port}",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ts_lua|conn_track|net_queue",
            "proxy.config.net.per_client.max_connections_in": LIMIT,
            "proxy.config.http.keep_alive_enabled_in": 0,
            "proxy.config.http.accept_no_activity_timeout": 300,
            "proxy.config.http.transaction_no_activity_timeout_in": 300,
        }
    )
    ats.start()

    held = hold("127.0.0.1", ats.http_port, LIMIT)
    try:
        wait_for_metrics(ats, {OPEN: LIMIT, THROTTLED: 0})
        outcome = attempt("127.0.0.1", ats.http_port)
        print(f"RESULT calls={calls} outcome={outcome} expected={expected}")
        assert outcome == expected, f"Connection {LIMIT + 1} from 127.0.0.1 was {outcome}, expected {expected}"
        # An exempt address is counted on every accept, and a refusal is counted as a throttle.
        if expected == "served":
            wait_for_metrics(ats, {THROTTLED: 0, EXEMPT: LIMIT + 1})
        else:
            wait_for_metrics(ats, {THROTTLED: 1, EXEMPT: 0})
    finally:
        for sock in held:
            sock.close()
