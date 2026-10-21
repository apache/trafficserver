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
"""HostDB can be cleared and repopulated without restarting ATS."""
import re
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory, Curl


def test_hostdb_clear(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Clear both idle and in-flight HostDB entries, then resolve again.

    :param ats_factory: Proxy factory.
    :param services: DNS and verifier factory.
    :param curl: Transport-aware client for the in-flight request.
    """
    replay = Path(__file__).parent / "replay/hostdb_clear.replay.yaml"
    hostname = "resolve.this.com"
    origin = services.verifier_server("origin", replay)
    dns = services.dns("dns", default="127.0.0.1")
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}", "proxy.config.dns.resolv_conf": "NULL"})
    ats.remap_config.add_line(f"map / http://{hostname}:{origin.http_port}/")
    dns.start()
    origin.start()
    ats.start()
    result = services.verifier_client("before-clear", replay, http_ports=[ats.http_port]).run()
    assert result.returncode == 0, result.output
    result = ats.traffic_ctl("hostdb", "status", hostname)
    assert result.returncode == 0 and hostname in result.stdout, result.output
    result = ats.traffic_ctl("hostdb", "clear")
    assert result.returncode == 0, result.output
    result = ats.traffic_ctl("metric", "get", "proxy.process.hostdb.cache.current_items")
    assert result.returncode == 0, result.output
    assert re.search(r"proxy.process.hostdb.cache.current_items 0$", result.stdout, re.MULTILINE), result.output
    with ThreadPoolExecutor(max_workers=1) as executor:
        future = executor.submit(
            curl.run_for, ats,
            f'--fail --silent --show-error -o /dev/null -H "Host: {hostname}" -H "X-Request: request" -H "uuid: 1" '
            f'http://127.0.0.1:{ats.http_port}/some/path')
        deadline = time.monotonic() + 5
        while True:
            result = ats.traffic_ctl("hostdb", "status", hostname)
            if result.returncode == 0 and hostname in result.stdout:
                break
            assert time.monotonic() < deadline, result.output
            time.sleep(0.05)
        result = ats.traffic_ctl("hostdb", "clear")
        assert result.returncode == 0, result.output
        result = future.result(timeout=30)
        assert result.returncode == 0, result.output
    result = ats.traffic_ctl("hostdb", "clear", "extra_arg")
    assert result.returncode == 64, result.output
    result = services.verifier_client("after-clear", replay, http_ports=[ats.http_port]).run()
    assert result.returncode == 0, result.output
    result = ats.traffic_ctl("hostdb", "status", hostname)
    assert result.returncode == 0 and hostname in result.stdout, result.output
    assert origin.is_running and dns.is_running and ats.is_running
    ats.stop()
    assert "HostDB cleared via JSONRPC" in ats.diags_log.read_text(errors="replace")
