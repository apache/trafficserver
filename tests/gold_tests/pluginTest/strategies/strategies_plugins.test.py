'''
'''
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

Test.Summary = '''
Combined header_rewrite/regex_remap/tslua strategies tests, run against both
remap.config and remap.yaml.
'''

Test.SkipUnless(
    Condition.PluginExists('header_rewrite.so'),
    Condition.PluginExists('regex_remap.so'),
    Condition.PluginExists('tslua.so'),
)
Test.ContinueOnFail = False

dns = Test.MakeDNServer("dns")

origins = []

chars = ['0', '1', '2', 'p', 's']
for char in chars:
    name = f"nh{char}"
    origin = Test.MakeOriginServer(name, options={"--verbose": ""})
    request_header = {
        "headers": f"GET / HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    request_header = {
        "headers": "GET /path HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nOrigin: {name}\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": name,
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    request_header = {
        "headers": f"GET /path/{name} HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nOrigin: {name}\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": name,
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    origin.ReturnCode = 0
    origins.append(origin)
    dns.addRecords(records={name: ["127.0.0.1"]})

# (tag, plugin, plugin parameter) for each plugin under test.
PLUGINS = [
    ("hr", "header_rewrite.so", "hdr_rw.config"),
    ("rr", "regex_remap.so", "regex_remap.config"),
    ("lua", "tslua.so", "strategies.lua"),
]

# (host prefix, remap rule strategy) for each mapping of every plugin.
MAPPINGS = [
    ("nhp", None),
    ("nhs", "nh0"),
    ("nh0", "nh0"),
    ("nh1", "nh1"),
    ("nh2", None),
]

# (description, curl url and arguments, expected origin)
REQUESTS = [
    # header_rewrite
    ("nhp_hr parent.config through request", "http://nhp_hr/path", "nh2"),
    ("nhs_hr straight through request", "http://nhs_hr/path", "nh0"),
    ("nh0_hr straight through request", "http://nh0_hr/path", "nh0"),
    ("nh1_hr straight through request", "http://nh1_hr/path", "nh1"),
    ("nh2_hr straight through request", "http://nh2_hr/path", "nh2"),
    ("nh0_hr switch to nh1", 'http://nh0_hr/path -H "Strategy: nh1"', "nh1"),
    ("nh1_hr switch to parent.config", 'http://nh1_hr/path -H "Strategy: null"', "nh2"),
    ("nh2_hr switch to nh0", 'http://nh2_hr/path -H "Strategy: nh0"', "nh0"),
    ("nh0_hr switch to nemo (fail)", 'http://nh0_hr/path -H "Strategy: nemo"', "nh0"),
    # regex_remap
    ("nhp_rr parent.config", "http://nhp_rr/nhp", "nh2"),
    ("nhs_rr strategies.yaml", "http://nhs_rr/nh", "nh0"),
    ("nh0_rr switch to nh1", "http://nh0_rr/nh0", "nh1"),
    ("nh1_rr switch to parent.config", "http://nh1_rr/nh1", "nh2"),
    ("nh2_rr switch to nh0", "http://nh2_rr/nh2", "nh0"),
    ("nh0_rr switch to nemo", "http://nh0_rr/nemo", "nh0"),
    # tslua
    ("nhp_lua parent.config", "http://nhp_lua/nh", "nh2"),
    ("nhs_lua strategies.yaml", "http://nhs_lua/nh", "nh0"),
    ("nh0_lua switch to nh1", "http://nh0_lua/nh0", "nh1"),
    ("nh1_lua switch to parent.config", "http://nh1_lua/nh1", "nh2"),
    ("nh2_lua switch to nh0", "http://nh2_lua/nh2", "nh0"),
    ("nh0_lua switch to nemo", "http://nh0_lua/nemo", "nh0"),
    # global header_rewrite, resolved per transaction
    ("nhp_hr global header_rewrite switches to nh1", 'http://nhp_hr/path -H "GlobalStrategy: nh1"', "nh1"),
    ("nhp_hr global header_rewrite expands name to nh0", 'http://nhp_hr/path -H "DynStrategy: nh0"', "nh0"),
]

# The 'nemo' cases must fail to resolve, and nothing else may log an ERROR.
EXPECTED_ERRORS = [
    (
        r"\[header_rewrite\] Failed to resolve strategy 'nemo' while loading the remap rule",
        "header_rewrite rejects an unknown strategy at load"),
    (r"\[regex_remap\] Unable to resolve strategy: 'nemo'", "regex_remap rejects an unknown strategy"),
    (
        r"\[ts_lua\]\[ts_lua_http_set_next_hop_strategy\] Failed get next hop strategy name 'nemo'",
        "ts_lua rejects an unknown strategy"),
]


def configure_plugins(ts) -> None:
    ts.Disk.MakeConfigFile("hdr_rw.config").AddLines(
        [
            'cond %{CLIENT-HEADER:Strategy} ="nemo"',
            "set-next-hop-strategy nemo",
            'cond %{CLIENT-HEADER:Strategy} ="nh0"',
            "set-next-hop-strategy nh0",
            'cond %{CLIENT-HEADER:Strategy} ="nh1"',
            "set-next-hop-strategy nh1",
            'cond %{CLIENT-HEADER:Strategy} ="null"',
            "set-next-hop-strategy null",
            'cond %{CLIENT-HEADER:Strategy} ="clear"',
            'set-next-hop-strategy ""',
        ])
    ts.Disk.MakeConfigFile("global_hdr_rw.config").AddLines(
        [
            "cond %{READ_REQUEST_HDR_HOOK} [AND]",
            'cond %{CLIENT-HEADER:GlobalStrategy} ="nh1"',
            "set-next-hop-strategy nh1",
            "cond %{READ_REQUEST_HDR_HOOK} [AND]",
            "cond %{CLIENT-HEADER:DynStrategy} /nh/",
            "set-next-hop-strategy %{CLIENT-HEADER:DynStrategy}",
        ])
    ts.Disk.plugin_config.AddLine("header_rewrite.so global_hdr_rw.config")
    ts.Disk.MakeConfigFile("regex_remap.config").AddLines(
        [
            "/nh0 http://origin/path @strategy=nh1",
            '/nh1 http://origin/path @strategy=',
            "/nh2 http://origin/path @strategy=nh0",
            '/null http://origin/path @strategy=null',
            "/nemo http://origin/path @strategy=nemo",
            "# fallthrough",
            "/ http://origin/path",
        ])
    ts.Disk.MakeConfigFile("strategies.lua").AddLines(
        [
            'function do_remap()',
            ' local uri = ts.client_request.get_uri()',
            ' if uri:find("nh0") then',
            '  ts.http.set_next_hop_strategy("nh1")',
            ' elseif uri:find("nh1") then',
            '  ts.http.set_next_hop_strategy("")',
            ' elseif uri:find("nh2") then',
            '  ts.http.set_next_hop_strategy("nh0")',
            ' elseif uri:find("null") then',
            '  ts.http.set_next_hop_strategy("null")',
            ' elseif uri:find("nemo") then',
            '  ts.http.set_next_hop_strategy("nemo")',
            ' end',
            ' ts.client_request.set_uri("path")',
            ' return 0',
            'end',
        ])


def configure_parents(ts) -> None:
    ts.Disk.parent_config.AddLines(
        [f'dest_domain=. parent="nh2:{origins[2].Variables.Port}" round_robin=false go_direct=false parent_is_proxy=false'])

    ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")
    s = ts.Disk.strategies
    s.AddLine("groups:")
    for char, origin in zip(chars, origins):
        s.AddLines(
            [
                f"  - &g{char}",
                f"    - host: nh{char}",
                f"      protocol:",
                f"      - scheme: http",
                f"        port: {origin.Variables.Port}",
                f"      weight: 1.0",
            ])
    s.AddLine("strategies:")
    for char in chars:
        s.AddLines(
            [
                f"  - strategy: nh{char}",
                f"    policy: consistent_hash",
                f"    hash_key: path",
                f"    go_direct: false",
                f"    parent_is_proxy: false",
                f"    ignore_self_detect: true",
                f"    groups:",
                f"      - *g{char}",
                f"    scheme: http",
            ])


def configure_remap_config(ts) -> None:
    lines = []
    for tag, plugin, pparam in PLUGINS:
        for host, strategy in MAPPINGS:
            strategy_arg = f" @strategy={strategy}" if strategy else ""
            lines.append(f"map http://{host}_{tag} http://origin{strategy_arg} @plugin={plugin} @pparam={pparam}")
    ts.Disk.remap_config.AddLines(lines)


def configure_remap_yaml(ts) -> None:
    lines = ["remap:"]
    for tag, plugin, pparam in PLUGINS:
        for host, strategy in MAPPINGS:
            lines += [
                "  - type: map",
                "    from:",
                f"      url: http://{host}_{tag}",
                "    to:",
                "      url: http://origin",
            ]
            if strategy:
                lines.append(f"    strategy: {strategy}")
            lines += [
                "    plugins:",
                f"      - name: {plugin}",
                "        params:",
                f"          - {pparam}",
            ]
    ts.Disk.remap_yaml.AddLines(lines)


def check_diags(ts) -> None:
    patterns = [pattern for pattern, _ in EXPECTED_ERRORS]
    ts.Disk.diags_log.Content = Testers.ContainsExpression(*EXPECTED_ERRORS[0])
    for pattern, description in EXPECTED_ERRORS[1:]:
        ts.Disk.diags_log.Content += Testers.ContainsExpression(pattern, description)
    ts.Disk.diags_log.Content += Testers.ExcludesExpression(
        rf"ERROR: (?!{'|'.join(patterns)})", "No ERROR other than the expected strategy failures")
    ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL", "No FATAL messages")


def run_variant(name: str, configure_remap, start_servers: bool) -> None:
    ts = Test.MakeATSProcess(name, enable_cache=False)
    ts.ReturnCode = 0
    ts.Disk.records_config.update(
        {
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': "NULL",
            'proxy.config.http.cache.http': 0,
            "proxy.config.http.insert_response_via_str": 1,
            'proxy.config.http.uncacheable_requests_bypass_parent': 0,
            'proxy.config.http.no_dns_just_forward_to_parent': 1,
            'proxy.config.http.parent_proxy.mark_down_hostdb': 0,
            'proxy.config.http.parent_proxy.self_detect': 0,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': "next_hop|dns|http|parent|regex_remap|header_rewrite|tslua",
        })
    configure_plugins(ts)
    configure_parents(ts)
    configure_remap(ts)
    check_diags(ts)

    # stdout for body, stderr for headers
    curl_and_args = f"-s -o /dev/stdout -D /dev/stderr -x localhost:{ts.Variables.port}"

    for index, (description, url, expected) in enumerate(REQUESTS):
        tr = Test.AddTestRun(f"{name}: {description}")
        ps = tr.Processes.Default
        if index == 0:
            if start_servers:
                for origin in origins:
                    ps.StartBefore(origin, ready=When.PortOpen(origin.Variables.Port))
                ps.StartBefore(dns)
            ps.StartBefore(ts)
        tr.MakeCurlCommand(f"{curl_and_args} {url}", ts=ts)
        ps.ReturnCode = 0
        ps.Streams.stdout.Content = Testers.ContainsExpression(expected, f"expected {expected}")
        tr.StillRunningAfter = ts
        tr.StillRunningAfter = dns
        for origin in origins:
            tr.StillRunningAfter = origin


run_variant("ts_remap_config", configure_remap_config, start_servers=True)
run_variant("ts_remap_yaml", configure_remap_yaml, start_servers=False)
