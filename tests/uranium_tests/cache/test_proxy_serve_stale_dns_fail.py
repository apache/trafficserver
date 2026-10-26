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

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold


def test_proxy_serve_stale_dns_fail(ats_factory: ATSFactory, curl: Curl, services: ServiceFactory) -> None:
    """A child and parent proxy serve stale content after DNS failure.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    :param services: Factory owning support services and their cleanup.
    """

    _SERVER_NAME = "http://unknown.domain.com/"
    # A stale copy may be served for max_stale_age seconds past its max-age. The
    # checks that expect a stale copy to be served get a window far longer than the
    # test runs, so a slow machine cannot push them past it. The checks that expect a
    # copy to be too old send X-Stale-Window: narrow, which shrinks the window to one
    # second for that request only, so a delay only makes them more certain. The
    # header does not change the cache key, so both kinds of request read the same
    # cached copy. Only the localhost maps load the rule, so the narrow window applies
    # on the proxy the client talks to directly, not on the parent the child forwards
    # to.
    wide_stale_age = 300
    narrow_stale_age = 1
    stale_window_rules = (
        "cond %{REMAP_PSEUDO_HOOK}\n"
        "cond %{CLIENT-HEADER:X-Stale-Window} =narrow\n"
        f"set-config proxy.config.http.cache.max_stale_age {narrow_stale_age}\n")
    narrow = '-H "X-Stale-Window: narrow"'
    dns = services.dns("dns")
    dns.add_records({"localhost": ["127.0.0.1"]})
    child = ats_factory.create("ts_child")
    parent = ats_factory.create("ts_parent", enable_uds=False)
    child.records.update(
        {
            "proxy.config.http.push_method_enabled": 1,
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.http.cache.max_stale_age": wide_stale_age,
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
        })
    child.parent_config.add_line(f"dest_domain=. parent=localhost:{parent.http_port} round_robin=consistent_hash go_direct=false")
    child.write_config_file("stale_window.conf", stale_window_rules)
    child.remap_config.add_line(
        f"map http://localhost:{child.http_port} {_SERVER_NAME} @plugin=header_rewrite.so @pparam=stale_window.conf")
    parent.records.update(
        {
            "proxy.config.http.push_method_enabled": 1,
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.http.cache.max_stale_age": wide_stale_age,
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
        })
    parent.write_config_file("stale_window.conf", stale_window_rules)
    parent.remap_config.add_lines(
        [
            f"map http://localhost:{parent.http_port} {_SERVER_NAME} @plugin=header_rewrite.so @pparam=stale_window.conf",
            f"map {_SERVER_NAME} {_SERVER_NAME}",
        ])
    dns.start()
    parent.start()
    child.start()
    stale_5 = (
        "HTTP/1.1 200 OK\nServer: ATS/10.0.0\nAccept-Ranges: bytes\nContent-Length: 6\n"
        "Cache-Control: public, max-age=5\n\nCACHED")
    stale_10 = stale_5.replace("max-age=5", "max-age=10")
    script = (
        f'{{curl}} -X PUSH -d "{stale_5}" "http://localhost:{child.http_port}";'
        f'{{curl}} -X PUSH -d "{stale_10}" "http://localhost:{parent.http_port}";'
        f"sleep 7; {{curl}} -s -v http://localhost:{child.http_port};"
        # Serving stale through negative revalidation rewrites the cached copy, which
        # restarts its age. Wait until it is past max-age again, so it is too old only
        # if the rewrite kept the max-age rather than taking the new Expires.
        f"sleep 7; {{curl}} -s -v {narrow} http://localhost:{child.http_port};"
        f'{{curl_base}} -X PUSH -d "{stale_5}" "http://localhost:{parent.http_port}";'
        f"sleep 7; {{curl_base}} -s -v http://localhost:{parent.http_port};"
        f"{{curl_base}} -s -v {narrow} http://localhost:{parent.http_port};")
    result = curl.run_script(child, script, timeout=70)

    assert result.returncode == 0, result.output
    assert_matches_gold(result.stderr, Path(__file__).parent / "gold/serve_stale_dns_fail.gold")
