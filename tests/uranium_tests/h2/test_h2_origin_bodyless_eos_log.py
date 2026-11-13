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
"""Log origin closes during the response header only when the header is incomplete."""
import re
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_file_lines

CLOSE_MESSAGE = "Server closed connection while reading response header."

# Path, curl options, and the status the client must receive. Each response
# ends on its HTTP/2 HEADERS frame, which the origin stream reports as EOS.
# The 304 requests are conditional: ATS accepts a Content-Length without a
# body on a 304 only in answer to one.
CONDITIONAL = "-H 'If-None-Match: \"v1\"'"
BODYLESS_CASES = [
    ("/head", "--head", 200),
    ("/no-content", "", 204),
    ("/not-modified", CONDITIONAL, 304),
    ("/not-modified-with-length", CONDITIONAL, 304),
    ("/empty-ok", "", 200),
    ("/redirect-zero-length", "", 302),
    ("/early-hints-no-content", "", 204),
]

# Origin closes that must still be logged, so that an empty error.log cannot
# pass the absence checks for the bodyless cases.
TRUNCATED_CASES = [
    ("h2.test", "/reset-before-headers"),
    ("h1.test", "/header-cut-at-line"),
]


def test_h2_origin_bodyless_eos_log(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Keep bodyless HTTP/2 responses out of error.log while logging truncated headers.

    :param ats_factory: Proxy factory.
    :param services: Origin process factory.
    :param curl: HTTP client.
    """
    h2_port = services.allocate_port()
    h1_port = services.allocate_port()
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin", [
            sys.executable,
            Path(__file__).with_name("bodyless_eos_origin.py"),
            str(h2_port),
            str(h1_port), ssl / "server.pem", ssl / "server.key"
        ],
        ready_port=h2_port)
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.http.connect_attempts_max_retries": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|http2",
        })
    ats.remap_config.add_line(f"map http://h2.test/ https://127.0.0.1:{h2_port}/")
    ats.remap_config.add_line(f"map http://h1.test/ http://127.0.0.1:{h1_port}/")
    origin.start()
    ats.start()

    for path, options, status in BODYLESS_CASES:
        result = curl.run_for(
            ats, f'--silent --show-error --max-time 10 {options} -o /dev/null -w "%{{http_code}}" '
            f'-H "Host: h2.test" http://127.0.0.1:{ats.http_port}{path}')
        assert result.returncode == 0, result.output
        assert result.stdout.strip() == str(status), f"{path}: {result.output}"

    # These responses are truncated, so only the error.log lines are checked.
    # The cut header promises a body that never arrives, so bound the wait.
    for host, path in TRUNCATED_CASES:
        curl.run_for(ats, f'--silent --max-time 2 -o /dev/null -H "Host: {host}" http://127.0.0.1:{ats.http_port}{path}')

    # The truncated cases ran last, so once their lines are flushed every
    # earlier line is in error.log too.
    for _, path in TRUNCATED_CASES:
        wait_for_file_lines(ats.error_log, rf"{re.escape(CLOSE_MESSAGE)} \(origin [^)]*{re.escape(path)}\)", 1, timeout=15)
    content = ats.error_log.read_text(errors="replace")
    logged = [line for line in content.splitlines() if CLOSE_MESSAGE in line]
    allowed = {path for _, path in TRUNCATED_CASES}
    unexpected = [
        line for line in logged if not (match := re.search(r"\(origin [^)]*?(/[^)/]*)\)", line)) or match.group(1) not in allowed
    ]
    assert not unexpected, f"Bodyless responses were logged as origin closes:\n{content}"
    assert origin.is_running
