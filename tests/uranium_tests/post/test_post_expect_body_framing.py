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
"""An unread Expect body must not be interpreted as a second request."""

import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize("mode", ("immediate", "delayed", "chunked"))
def test_post_expect_body_framing(ats_factory: ATSFactory, services: ServiceFactory, mode: str) -> None:
    """Require one response and close connections whose body cannot be drained.

    :param ats_factory: Proxy factory.
    :param services: Early-response origin factory.
    :param mode: How the client sends the request-shaped POST body.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process("origin", [sys.executable, directory.parents[1] / "tools/mock_origin.py", str(port)], ready_port=port)
    ats = ats_factory.create("ts", enable_cache=False)
    ats.remap_config.add_line(f"map /{mode} http://127.0.0.1:{port}")
    origin.start()
    ats.start()
    result = ats.run(
        sys.executable,
        directory / "expect_client.py",
        "127.0.0.1",
        str(ats.http_port),
        "--mode",
        mode,
        "--path",
        f"/{mode}",
        "-s",
        "example.com",
    )
    assert result.returncode == 0, result.output
    assert "RESPONSES_RECEIVED=1" in result.stdout, result.output
    assert "X-Smuggled" not in result.stdout, result.output
    if mode != "immediate":
        assert "CONNECTION_CLOSE_HEADER=yes" in result.stdout, result.output
