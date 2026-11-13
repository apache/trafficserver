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
"""A parent retry must reach the parent that selection moved on to."""

from pathlib import Path
import sys

from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_file_lines

# The failing parent answers every request with 503 and keeps the default
# request logging, so the captured stderr records the attempt it received.
_PARENT_A_SCRIPT = """
import http.server
import sys


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(503)
        self.send_header("Content-Length", "0")
        self.end_headers()


http.server.HTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
"""


def test_stale_parent_dst_addr(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A failed lookup must not pin the next parent's attempt to a stale address.

    The first parent answers 503, so the request is retried against the second
    parent, whose name deliberately does not resolve. That failed lookup makes
    the transaction select the third parent. Selection moves on, but the
    first parent's resolved address is still in the transaction; if it
    survives, PPDNSLookup's ``dst_addr.isValid()`` gate skips the lookup for
    the third parent and the attempt is connected to the first parent's
    address again, so the healthy third parent is never contacted and the
    client never sees the object.

    The response-retry path itself is not the subject here: a retry after a
    parent response always re-enters the DNS lookup on master, which is why
    this test moves selection on through a FAILED lookup instead.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    replay = Path(__file__).parent / "replays/parent_stale_dst_addr.replay.yaml"

    parent_a_port = services.allocate_port()
    parent_a = services.process(
        "parent-a",
        [sys.executable, "-c", _PARENT_A_SCRIPT, str(parent_a_port)],
        ready_port=parent_a_port,
    )
    parent_a.stderr.contains(r'"GET [^"]*/object HTTP/1\.1" 503', "the first attempt reached the failing parent")
    parent_c = services.verifier_server("parent-c", replay)

    # parent-a and parent-c resolve to loopback; parent-b's name is never
    # mapped and the service has no default answer, so its lookup fails — that
    # failure is what moves selection on to parent-c with the failed parents'
    # addresses still in the transaction.
    dns = services.dns("dns")
    dns.add_records({"parent-a.test": ["127.0.0.1"], "parent-c.test": ["127.0.0.1"]})
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_trans|parent_select",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.parent_proxy.self_detect": 0,
            # The cache is disabled, so every request is non-lookupable and the
            # default (1) would bypass the parent and go straight to the
            # origin, which never reaches the retry under test.
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            # The deployment forwards to its parents without resolving the
            # origin (the documented parent-proxy mode), so the origin name
            # never needs to resolve and every attempt is a parent attempt.
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            # The retry under test is the simple-retry response path, so no
            # connect attempt is spent retrying one parent.
            "proxy.config.http.parent_proxy.total_connect_attempts": 1,
            "proxy.config.http.parent_proxy.per_parent_connect_attempts": 1,
            "proxy.config.url_remap.remap_required": 0,
        }
    )
    # round_robin=false makes the walk deterministic: parent-a is selected
    # first, the simple retry takes the next parent (parent-b), and parent-b's
    # failed lookup takes the next parent again (parent-c).
    ats.parent_config.add_line(
        f'dest_domain=. parent="parent-a.test:{parent_a_port};parent-b.invalid:{parent_c.http_port};'
        f'parent-c.test:{parent_c.http_port}" '
        'round_robin=false go_direct=false parent_is_proxy=true '
        'parent_retry=simple_retry simple_server_retry_responses="503" max_simple_retries=1'
    )
    ats.allow_private_connect(methods=("GET",))

    dns.start()
    parent_a.start()
    parent_c.start()
    ats.start()
    result = services.verifier_client("client", replay, http_ports=[ats.http_port]).run()
    assert result.returncode == 0, result.output
    assert "uuid: stale-parent-retry" in result.output

    # PPDNSLookup logs to traffic.out the address it resolved for the selected
    # parent, so this identifies the parent the attempt after the failed
    # lookup was sent to.
    wait_for_file_lines(
        ats.traffic_out,
        rf"DNS lookup for successful IP: 127\.0\.0\.1:{parent_c.http_port}\b",
        1,
    )
