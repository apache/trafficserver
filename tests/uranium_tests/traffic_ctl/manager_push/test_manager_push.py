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

import threading
import time

from tools.uranium.services import ATS, ATSFactory, Curl

# Counted when a client connection is accepted, before remap can reject the
# request, so one curl request always moves it.
METRIC = "proxy.process.http.total_incoming_connections"


def send_messages(stop: threading.Event, *, _ats: ATS) -> None:
    """Keep traffic_manager sending traffic_server a message every quarter second.

    :param stop: Event that ends the message stream.
    :param _ats: Test-local ats configured by the test.
    """

    while not stop.is_set():
        _ats.traffic_ctl("plugin", "msg", "uranium.manager_push", "ping")
        stop.wait(0.25)


def wait_for_metric(name: str, *, _ats: ATS, timeout: float) -> str:
    """Wait until traffic_manager reports a positive value for @a name.

    :param name: Metric name.
    :param _ats: Test-local ats configured by the test.
    :param timeout: Maximum number of seconds to wait.
    """

    deadline = time.monotonic() + timeout
    output = ""
    while time.monotonic() < deadline:
        result = _ats.traffic_ctl("metric", "get", name)
        output = result.output
        if result.returncode == 0 and int(result.stdout.split()[-1]) > 0:
            return output
        time.sleep(0.1)
    raise AssertionError(f"Metric {name} did not reach traffic_manager:\n{output}")


def test_metrics_reach_manager_during_manager_messages(ats_factory: ATSFactory, curl: Curl) -> None:
    """traffic_server pushes its metrics while traffic_manager keeps messaging it.

    traffic_ctl reads metrics from traffic_manager, which only learns them
    from traffic_server's periodic record pushes. A steady stream of messages
    from traffic_manager must not keep those pushes from going out.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """

    ats = ats_factory.create("ts", enable_cache=False, command="traffic_manager")
    ats.start()

    stop = threading.Event()
    sender = threading.Thread(target=send_messages, args=(stop,), kwargs={"_ats": ats})
    sender.start()
    try:
        result = curl.get(ats, "/")
        assert result.returncode == 0, result.output
        # Pushes go out every remote_sync_interval_ms, 5 seconds by default.
        wait_for_metric(METRIC, _ats=ats, timeout=15)
    finally:
        stop.set()
        sender.join()
