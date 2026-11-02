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

from dataclasses import dataclass
from pathlib import Path
import shlex
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl

TEST_DIRECTORY = Path(__file__).parent


@dataclass(frozen=True)
class AcceptTimeoutCase:
    """Describe a TCP or TLS connection with optional request bytes."""

    name: str
    tls: bool
    incomplete_request: bool
    expected_message: str


CASES = (
    AcceptTimeoutCase("tls-no-data", True, False, "Accept timeout"),
    AcceptTimeoutCase("tls-incomplete-header", True, True, "Transaction inactivity timeout"),
    AcceptTimeoutCase("tcp-no-data", False, False, "Accept timeout"),
    AcceptTimeoutCase("tcp-incomplete-header", False, True, "Transaction inactivity timeout"),
)


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure distinct accept, transaction, and default timeouts.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.records.update(
        {
            "proxy.config.http.transaction_no_activity_timeout_in": 6,
            "proxy.config.http.accept_no_activity_timeout": 2,
            "proxy.config.net.default_inactivity_timeout": 10,
            "proxy.config.net.defer_accept": 0,
        })
    return ats


def client_command(*, _ats: ATS, _case: AcceptTimeoutCase) -> str:
    """Build the original timing wrapper around the selected client.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    """

    timer = shlex.quote(str(TEST_DIRECTORY / "time_client.sh"))
    if _case.tls:
        client = f"openssl s_client -ign_eof -connect 127.0.0.1:{_ats.https_port}"
        command = f"bash {timer} {shlex.quote(client)}"
        return f"printf 'GET /.html HTTP/1.1' | {command}" if _case.incomplete_request else command
    if _case.incomplete_request:
        request = shlex.quote(str(TEST_DIRECTORY / "create_request.sh"))
        client = f"nc -c {request} 127.0.0.1 {_ats.http_port}"
    else:
        client = f"telnet 127.0.0.1 {_ats.http_port}"
    return f"bash {timer} {shlex.quote(client)}"


def verify(result: CommandResult, *, _case: AcceptTimeoutCase) -> None:
    """Require the connection to expire in its expected timeout bucket.

    :param _case: Test-local case configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert _case.expected_message in result.stdout


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.name)
def test_accept_timeout(case: AcceptTimeoutCase, ats_factory: ATSFactory, curl: Curl) -> None:
    """ATS applies accept and transaction inactivity timeouts at the right stage.

    :param case: Case used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """

    if curl.uses_uds:
        pytest.skip("raw TCP connections require a TCP listener")
    required = ("openssl",) if case.tls else (("nc",) if case.incomplete_request else ("telnet",))
    missing = [program for program in required if shutil.which(program) is None]
    if missing:
        pytest.skip(f"required program is unavailable: {', '.join(missing)}")
    _ats = configure_ats(ats_factory)

    _ats.start()
    verify(_ats.run_shell(client_command(_ats=_ats, _case=case), timeout=20), _case=case)
