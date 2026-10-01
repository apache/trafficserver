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

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory

THREAD_CONFIGURATION_CASES = tuple(
    (execution, *other) for execution in (1, 2, 32, 100) for other in ((0, 1, 1), (1, 2, 8), (10, 10, 32)))


def configure_ats(execution: int, accept: int, task: int, aio: int, *, _ats_factory: ATSFactory) -> ATS:
    """Configure one ATS process for the requested thread counts.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param execution: Execution used by this test step.
    :param accept: Accept used by this test step.
    :param task: Task used by this test step.
    :param aio: Aio used by this test step.
    """

    ats = _ats_factory.create(f"ts-{execution}-exec-{accept}-accept-{task}-task-{aio}-aio")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.5,
            "proxy.config.exec_thread.limit": execution,
            "proxy.config.accept_threads": accept,
            "proxy.config.task_threads": task,
            "proxy.config.cache.threads_per_disk": aio,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "iocore_thread_start|iocore_net_accept_start",
        })
    return ats


def check_threads(ats: ATS, execution: int, accept: int, task: int, aio: int, *, _checker: Path) -> None:
    """Run the process-level thread inspector against @a ats.

    :param _checker: Test-local checker configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param execution: Execution used by this test step.
    :param accept: Accept used by this test step.
    :param task: Task used by this test step.
    :param aio: Aio used by this test step.
    """

    result = ats.run(
        sys.executable,
        _checker,
        "-p",
        ats.run_directory,
        "-e",
        str(execution),
        "-a",
        str(accept),
        "-t",
        str(task),
        "-c",
        str(aio),
        timeout=20,
    )
    assert result.returncode == 0, result.output


def test_thread_config(ats_factory: ATSFactory) -> None:
    """Traffic Server honors explicit thread configuration on Linux.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    if not sys.platform.startswith("linux"):
        pytest.skip("Thread names are validated only on Linux")
    _checker = Path(__file__).with_name("check_threads.py")

    for execution, accept, task, aio in THREAD_CONFIGURATION_CASES:
        ats = configure_ats(execution, accept, task, aio, _ats_factory=ats_factory)
        ats.start()
        check_threads(ats, execution, accept, task, aio, _checker=_checker)
        ats.stop()
