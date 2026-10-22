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
"""Never pool an origin connection that is still owed a request body."""
import re
import shlex
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize(
    "case,args", (
        ("expect", "--expect-100-continue"),
        ("plain", ""),
        ("interim", "--expect-100-continue --path /interim"),
        ("reuse", "--complete-body --path /complete"),
    ))
def test_post_early_response_100_continue(ats_factory: ATSFactory, services: ServiceFactory, case: str, args: str) -> None:
    """Check both owed-body refusal and legitimate reuse after a complete body.

    :param ats_factory: Proxy factory.
    :param services: Custom origin factory.
    :param case: Withheld-body variant or completed-body control.
    :param args: Arguments selecting the custom client's wire behavior.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process("origin", [sys.executable, directory / "early_response_100c_origin.py", str(port)], ready_port=port)
    ats = ats_factory.create("ts", enable_cache=False)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_hdrs",
            "proxy.config.http.send_100_continue_response": 0,
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.exec_thread.autoconfig": 0,
            "proxy.config.http.server_session_sharing.pool": "global",
        })
    if case == "reuse":
        for expression in ("SAW-EXPECT 1", "BODY-COMPLETE 16 of 16", "REQUEST 2 GET /probe"):
            origin.stdout.contains(expression, "A completed POST must allow reuse of the same origin connection.")
    else:
        path = "/interim" if case == "interim" else "/early"
        origin.stdout.contains(f"REQUEST 1 POST {path}", "The origin must receive the early POST.")
        origin.stdout.contains("DRAINED 0 of 100000", "No following request may be drained as POST body.")
        origin.stdout.excludes("CONSUMED-AS-BODY", "Request lines must not be consumed as an earlier body.")
    origin.start()
    ats.start()
    result = ats.run(sys.executable, directory / "early_response_100c_client.py", str(ats.http_port), *shlex.split(args))
    assert result.returncode == 0, result.output
    assert origin.is_running
    if case == "reuse":
        assert "COMPLETE-POST STATUS HTTP/1.1 200 OK" in result.stdout, result.output
    else:
        assert "EARLY-POST CLIENT-CLOSE True" in result.stdout, result.output
        assert "PROBE STATUS HTTP/1.1 200 OK" in result.stdout, result.output
        if case == "plain":
            assert re.search(r"EARLY-POST STATUS HTTP/1\.1 420", result.stdout), result.output
    ats.stop()
    assert ("Origin is owed request body bytes" in ats.traffic_out.read_text()) == (case != "reuse")
