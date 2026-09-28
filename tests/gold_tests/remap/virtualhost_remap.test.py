'''
Verify that per-domain remap tables in virtualhost.yaml are applied to requests.
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
Verify virtualhost.yaml domain resolution and per-domain remap rules on the request path.
'''

import os

Test.ContinueOnFail = True
Test.testName = 'virtualhost_remap'

ts = Test.MakeATSProcess("ts")

# The origin is keyed on the request path only (the default lookup key), so each
# remap rule can be given a private target path. Which rule won is then decided
# by which body comes back, not merely by getting a 200.
server = Test.MakeOriginServer("server")


def add_origin_response(path: str, body: str) -> None:
    """Register an origin response for `path` carrying a rule-specific body."""
    request_header = {
        "headers": f"GET {path} HTTP/1.1\r\nHost: origin.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": body}
    server.addResponse("sessionfile.log", request_header, response_header)


# One target path per remap rule that could plausibly fire. The bodies are chosen
# so that no expected body is a substring of another.
add_origin_response("/vhost-exact-domain/", "hit:vhost-exact-domain")
add_origin_response("/vhost-deep-wildcard/", "hit:vhost-deep-wildcard")
add_origin_response("/vhost-wide-wildcard/", "hit:vhost-wide-wildcard")
add_origin_response("/vhost-wildcard-precedence/", "hit:vhost-wildcard-precedence")
add_origin_response("/vhost-exact-precedence/", "hit:vhost-exact-precedence")
add_origin_response("/vhost-fallback-rule/", "hit:vhost-fallback-rule")
add_origin_response("/global-fallback/other/", "hit:global-fallback")
add_origin_response("/global-plain/", "hit:global-plain")
# Only reachable if the rejected reload below is wrongly published.
add_origin_response("/vhost-conflict/", "hit:vhost-conflict")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'virtualhost|url_rewrite',
})

# The refused reload at the end of this test logs the conflict as an ERROR, which
# replaces the default diags expectations.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "is already claimed by virtualhost 'exact-only'", "The conflicting reload should name the virtualhost holding the domain")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL:", "A refused reload must not be fatal")

origin = f'127.0.0.1:{server.Variables.Port}'

# Global table, in remap.config (legacy) format. The virtualhost tables below are
# always YAML, so this also covers the mixed case.
ts.Disk.remap_config.AddLines(
    [
        f'map http://fallback.example.net/ http://{origin}/global-fallback/',
        f'map http://none.example.net/ http://{origin}/global-plain/',
    ])

vhost_config_lines = [
    'virtualhost:',
    # Plain exact-domain match. No wildcard in this config matches .example.org.
    '  - id: exact-only',
    '    domains:',
    '      - exact.example.org',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://exact.example.org/',
    '        to:',
    f'          url: http://{origin}/vhost-exact-domain/',
    # x.deep.example.com matches both this wildcard and the wider one below.
    # The longest (most specific) suffix must win.
    '  - id: deep-wildcard',
    '    domains:',
    '      - "*.deep.example.com"',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://x.deep.example.com/',
    '        to:',
    f'          url: http://{origin}/vhost-deep-wildcard/',
    '  - id: wide-wildcard',
    '    domains:',
    '      - "*.example.com"',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://x.deep.example.com/',
    '        to:',
    f'          url: http://{origin}/vhost-wide-wildcard/',
    '      - type: map',
    '        from:',
    '          url: http://precedence.example.com/',
    '        to:',
    f'          url: http://{origin}/vhost-wildcard-precedence/',
    # precedence.example.com is claimed exactly here and by the wildcard
    # above. The exact domain must win.
    '  - id: exact-precedence',
    '    domains:',
    '      - precedence.example.com',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://precedence.example.com/',
    '        to:',
    f'          url: http://{origin}/vhost-exact-precedence/',
    # This virtualhost resolves for fallback.example.net but its only rule
    # covers a different path, so requests elsewhere must fall back to the
    # global table.
    '  - id: path-miss',
    '    domains:',
    '      - fallback.example.net',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://fallback.example.net/only-here/',
    '        to:',
    f'          url: http://{origin}/vhost-fallback-rule/',
]

ts.Disk.virtualhost_yaml.AddLines(vhost_config_lines)


def add_request(name: str, host: str, path: str, expected: str, not_expected: str = "") -> 'TestRun':
    """Send one request through the proxy and assert which remap rule served it.

    :param name: Test run name.
    :param host: Host header, which selects the virtualhost.
    :param path: Request path.
    :param expected: Body of the rule that must have won.
    :param not_expected: Body of the rule that must have lost, if any.
    """
    tr = Test.AddTestRun(name)
    tr.MakeCurlCommand(f'-s -H"Host: {host}" http://127.0.0.1:{ts.Variables.port}{path} --verbose', ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(expected, f"{host}{path} should be served by {expected}")
    if not_expected:
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            not_expected, f"{host}{path} must not be served by {not_expected}")
    tr.StillRunningAfter = ts
    tr.StillRunningAfter += server
    return tr


# The first run starts the processes.
tr = add_request(
    "Exact virtualhost domain uses its own remap table", "exact.example.org", "/", "hit:vhost-exact-domain", "hit:global-plain")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)

add_request("Longest wildcard suffix wins", "x.deep.example.com", "/", "hit:vhost-deep-wildcard", "hit:vhost-wide-wildcard")

add_request(
    "Exact domain takes precedence over a wildcard", "precedence.example.com", "/", "hit:vhost-exact-precedence",
    "hit:vhost-wildcard-precedence")

add_request(
    "A virtualhost whose rules do not match falls back to the global table", "fallback.example.net", "/other/",
    "hit:global-fallback", "hit:vhost-fallback-rule")

add_request("A host with no virtualhost entry uses the global table", "none.example.net", "/", "hit:global-plain")

# ============================================================================
# A reload that is refused must leave the previous routing table serving.
#
# deep-wildcard is rewritten to also claim exact.example.org, which exact-only
# already holds. That conflict is detected by set_entry(), inside the critical
# section and after the copy of the live config has already dropped the old
# deep-wildcard entry — so this exercises the one failure path that runs after
# the read-copy-modify begins. Nothing may be published, and both the entry
# that was reloaded and the entry it collided with must still serve their
# original rules.
# ============================================================================
vhost_config_path = os.path.join(ts.Variables.CONFIGDIR, 'virtualhost.yaml')

conflicting_config_lines = [
    'virtualhost:',
    '  - id: exact-only',
    '    domains:',
    '      - exact.example.org',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://exact.example.org/',
    '        to:',
    f'          url: http://{origin}/vhost-exact-domain/',
    '  - id: deep-wildcard',
    '    domains:',
    '      - "*.deep.example.com"',
    # Already claimed by exact-only above.
    '      - exact.example.org',
    '    remap:',
    '      - type: map',
    '        from:',
    '          url: http://x.deep.example.com/',
    '        to:',
    f'          url: http://{origin}/vhost-conflict/',
    '      - type: map',
    '        from:',
    '          url: http://exact.example.org/',
    '        to:',
    f'          url: http://{origin}/vhost-conflict/',
]


def write_conflicting_config() -> None:
    """Replace virtualhost.yaml with a version whose deep-wildcard entry steals a claimed domain."""
    with open(vhost_config_path, 'w') as f:
        f.write("\n".join(conflicting_config_lines) + "\n")


tr = Test.AddTestRun("Rewrite virtualhost.yaml so deep-wildcard claims a domain exact-only holds")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'echo "rewrite virtualhost.yaml with a domain conflict"'
tr.Processes.Default.Setup.Lambda(lambda: write_conflicting_config())
tr.StillRunningAfter = ts

Test.AddConfigReload(
    ts,
    expect="fail",
    directives={"virtualhost.id": "deep-wildcard"},
    expect_tasks={"virtualhost": "fail"},
    delay_start=2,
    description="Single-entry reload with a claimed domain is refused")

add_request(
    "The refused reload leaves the reloaded entry serving its old rules", "x.deep.example.com", "/", "hit:vhost-deep-wildcard",
    "hit:vhost-conflict")

add_request(
    "The refused reload leaves the entry it collided with serving", "exact.example.org", "/", "hit:vhost-exact-domain",
    "hit:vhost-conflict")
