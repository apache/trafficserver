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
"""Exercise the 9.2 origin early-response release paths."""
import re
import sys
from pathlib import Path
from tools.uranium.services import ATS, ServiceFactory


def test_early_response_server_reuse(ats: ATS, services: ServiceFactory) -> None:
    """Do not pool a connection with an unfinished request body.

    :param ats: Isolated cache-enabled proxy.
    :param services: Owner of the bespoke origin.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process("origin", [sys.executable, directory / "early_response_origin.py", str(port)], ready_port=port)
    origin.stdout.excludes("CONSUMED-AS-BODY", "A later request must not be swallowed as body.")
    origin.stdout.contains("REQUEST GET /probe", "The following request reaches the origin.")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.exec_thread.autoconfig": 0,
        })
    origin.start()
    ats.start()
    for scenario, expected in (
        ("abandoned-tunnel", r"EARLY-POST STATUS HTTP/1\.1 420 Be Calm"),
        ("revalidate-304", r"REVALIDATE STATUS HTTP/1\.1 200 OK"),
    ):
        result = ats.run(sys.executable, directory / "early_response_client.py", str(ats.http_port), scenario)
        assert result.returncode == 0, result.output
        assert re.search(expected, result.stdout), result.output
        assert "PROBE STATUS HTTP/1.1 200 OK" in result.stdout, result.output
    origin.stop()
