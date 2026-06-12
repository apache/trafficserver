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
'''
Verify outbound ip_allow filtering for CONNECT destinations.
'''

Test.Summary = '''
Verify outbound ip_allow filtering for CONNECT destinations.
'''

Test.SkipIf(Condition.CurlUsingUnixDomainSocket())
Test.ContinueOnFail = True

server = Test.MakeOriginServer("server", ssl=True)

request = {
    "headers": f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{server.Variables.SSL_Port}\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}
response = {
    "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}
server.addResponse("sessionlog.json", request, response)


def configure_ts(name):
    ts = Test.MakeATSProcess(name, enable_cache=False)
    ts.Disk.records_config.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ip_allow",
            "proxy.config.http.connect_ports": f"{server.Variables.SSL_Port}",
            "proxy.config.url_remap.remap_required": 0,
        })
    return ts


ts_default = configure_ts("ts-default")
ts_allowed = configure_ts("ts-allowed")
ts_allowed.Disk.ip_allow_yaml.AddLines(
    [
        "ip_allow:",
        "  - apply: in",
        "    ip_addrs: 127.0.0.1",
        "    action: allow",
        "    methods: ALL",
        "  - apply: out",
        "    ip_addrs: 127.0.0.1",
        "    action: allow",
        "    methods: CONNECT",
        "  - apply: out",
        "    ip_addrs:",
        "      - 0.0.0.0/8",
        "      - 127.0.0.0/8",
        "      - \"::\"",
        "      - ::1",
        "      - 10.0.0.0/8",
        "      - 172.16.0.0/12",
        "      - 192.168.0.0/16",
        "      - 169.254.0.0/16",
        "      - ::/96",
        "      - fc00::/7",
        "      - fe80::/10",
        "      - ::ffff:0:0/96",
        "    action: deny",
        "    methods: CONNECT",
    ])

ts_sni_default = Test.MakeATSProcess("ts-sni-default", enable_cache=False, enable_tls=True)
ts_sni_default.addDefaultSSLFiles()
ts_sni_default.Disk.records_config.update(
    {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|ip_allow|ssl|sni",
        "proxy.config.http.connect_ports": f"{server.Variables.SSL_Port}",
        "proxy.config.ssl.server.cert.path": ts_sni_default.Variables.SSLDir,
        "proxy.config.ssl.server.private_key.path": ts_sni_default.Variables.SSLDir,
    })
ts_sni_default.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts_sni_default.Disk.sni_yaml.AddLines(
    [
        "sni:",
        "- fqdn: sni-denied.example.com",
        f"  tunnel_route: 127.0.0.1:{server.Variables.SSL_Port}",
    ])
ts_sni_default.Disk.diags_log.Content += Testers.ContainsExpression(
    r"server '127\.0\.0\.1.*' prohibited by ip-allow policy", "SNI tunnel_route should be denied by outbound ip_allow.")

loopback_url = f"https://127.0.0.1:{server.Variables.SSL_Port}/"
unspecified_url = f"https://0.0.0.0:{server.Variables.SSL_Port}/"
reserved_ipv4_url = f"https://0.1.2.3:{server.Variables.SSL_Port}/"
unspecified_v6_url = f"https://[::]:{server.Variables.SSL_Port}/"
compatible_loopback_url = f"https://[::7f00:1]:{server.Variables.SSL_Port}/"
mapped_loopback_url = f"https://[::ffff:127.0.0.1]:{server.Variables.SSL_Port}/"
mapped_loopback_hex_url = f"https://[::ffff:7f00:1]:{server.Variables.SSL_Port}/"


def add_denied_connect_run(name, url, http_connect="403"):
    tr = Test.AddTestRun(name)
    tr.MakeCurlCommand(
        f'-sk --noproxy does-not-match --proxy http://127.0.0.1:{ts_default.Variables.port} '
        f'-o /dev/null -w "http_code=%{{http_code}} http_connect=%{{http_connect}}\\n" {url}',
        ts=ts_default)
    tr.Processes.Default.ReturnCode = 56
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
        f"http_code=000 http_connect={http_connect}", "CONNECT should be rejected before tunneling.")
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts_default
    return tr


tr = Test.AddTestRun("Default policy denies CONNECT to loopback")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.SSL_Port))
tr.Processes.Default.StartBefore(ts_default)
tr.MakeCurlCommand(
    f'-sk --noproxy does-not-match --proxy http://127.0.0.1:{ts_default.Variables.port} '
    f'-o /dev/null -w "http_code=%{{http_code}} http_connect=%{{http_connect}}\\n" {loopback_url}',
    ts=ts_default)
tr.Processes.Default.ReturnCode = 56
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "http_code=000 http_connect=403", "CONNECT to loopback should be rejected by the default outbound policy.")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts_default

add_denied_connect_run("ATS rejects CONNECT to unspecified IPv4 before outbound policy", unspecified_url, http_connect="400")
add_denied_connect_run("Default policy denies CONNECT to 0.0.0.0/8", reserved_ipv4_url)
add_denied_connect_run("ATS rejects CONNECT to unspecified IPv6 before outbound policy", unspecified_v6_url, http_connect="400")
add_denied_connect_run("Default policy denies CONNECT to IPv4-compatible loopback", compatible_loopback_url)
add_denied_connect_run("Default policy denies CONNECT to IPv4-mapped loopback", mapped_loopback_url)
add_denied_connect_run("Default policy denies CONNECT to hex IPv4-mapped loopback", mapped_loopback_hex_url)

tr = Test.AddTestRun("Default policy denies SNI tunnel_route to loopback")
tr.Processes.Default.StartBefore(ts_sni_default)
tr.MakeCurlCommand(
    f"-skv --resolve sni-denied.example.com:{ts_sni_default.Variables.ssl_port}:127.0.0.1 "
    f"https://sni-denied.example.com:{ts_sni_default.Variables.ssl_port}/",
    ts=ts_sni_default)
tr.Processes.Default.ReturnCode = Any(35, 52, 56)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts_sni_default

tr = Test.AddTestRun("Explicit outbound allow permits CONNECT to loopback")
tr.Processes.Default.StartBefore(ts_allowed)
tr.MakeCurlCommand(
    f'-sk --noproxy does-not-match --proxy http://127.0.0.1:{ts_allowed.Variables.port} '
    f'-o /dev/null -w "http_code=%{{http_code}} http_connect=%{{http_connect}}\\n" {loopback_url}',
    ts=ts_allowed)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "http_code=200 http_connect=200", "Explicit outbound allow should permit the CONNECT tunnel.")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts_allowed
