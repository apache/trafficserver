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
"""SIGTERM must finish draining when listeners are closed, with and without dedicated accept threads."""
import signal

import pytest

from tools.uranium.services import ATS


@pytest.mark.parametrize("accept_threads", [0, 1])
def test_stop_listening_sigterm_exits(ats: ATS, accept_threads: int) -> None:
    """Closing listeners on SIGTERM must not wait on an accept thread's mutex.

    :param ats: Proxy whose expected exit is explicitly awaited.
    :param accept_threads: Value of proxy.config.accept_threads.
    """
    ats.records.update(
        {
            "proxy.config.accept_threads": accept_threads,
            "proxy.config.restart.stop_listening": 1,
            "proxy.config.stop.shutdown_timeout": 3,
        })
    ats.start()
    ats.send_signal(signal.SIGTERM)
    ats.wait(timeout=20)
    assert not ats.is_running
