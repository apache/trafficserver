'''
Test header_rewrite with URL conditions and operators.
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

import ats_autest

Test.Summary = '''
A set of remap rules and tests for header_rewrite.
'''

# Setup ATS and origin, define some convenience variables
Test.ContinueOnFail = True
run_dir = Test.RunDirectory

ts = Test.MakeATSProcess("ts")
ats_port = ts.Variables.port

server = Test.MakeOriginServer("server")
origin_port = server.Variables.Port

# Common debug logging config
ts.Disk.records_config.update(
    {
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.show_location': 0,
        'proxy.config.diags.debug.tags': 'header_rewrite',
    })

# ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"

# Install all the HRW configuration files
remap_rules = ['rule_client.conf', 'set_redirect.conf', 'rule_cond_method.conf', 'rule_l_value.conf']
for rule in remap_rules:
    ts.Setup.CopyAs(f'rules/{rule}', Test.RunDirectory)

# Add responses for all expected requests
resp_hdr = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
requests = [
    {
        "headers": "GET / HTTP/1.1\r\nHost: no_path.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }, {
        "headers": "GET /to_1/hello?=foo=bar HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }, {
        "headers": "GET /to_1/hrw-sets.png HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "GET /to_2/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "GET /to_3/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
]

for req in requests:
    server.addResponse("sessionfile.log", req, resp_hdr)

# remap.config: (from, to, rules)
url_base = "www.example.com/from"
origin_base = f'127.0.0.1:{origin_port}/to'

remap_rules = [
    ats_autest.RemapRule(
        from_url="no_path.com/", to_url="no_path.com?name=brian/", plugins=[("header_rewrite", [f"{run_dir}/set_redirect.conf"])]),
    ats_autest.RemapRule(
        from_url=f'{url_base}_1/', to_url=f'{origin_base}_1/', plugins=[("header_rewrite", [f"{run_dir}/rule_client.conf"])]),
    ats_autest.RemapRule(
        from_url=f'{url_base}_2/', to_url=f'{origin_base}_2/', plugins=[("header_rewrite", [f"{run_dir}/rule_cond_method.conf"])]),
    ats_autest.RemapRule(
        from_url=f'{url_base}_3/', to_url=f'{origin_base}_3/', plugins=[("header_rewrite", [f"{run_dir}/rule_l_value.conf"])]),
]

for rule in remap_rules:
    plugin_args = " ".join(f'@plugin={plugin}.so ' + " ".join(f'@pparam={p}' for p in params) for plugin, params in rule.plugins)
    ts.Disk.remap_config.AddLine(f'map http://{rule.from_url} http://{rule.to_url} {plugin_args}')
    ts.Disk.remap_config.AddLine(f'map https://{rule.from_url} http://{rule.to_url} {plugin_args}')

# Create all the test cases
curl_opt = f'--proxy 127.0.0.1:{ats_port} --verbose'
expected_log = "gold/header_rewrite-tag.gold"

test_runs = [
    {
        "desc": "TO-URL redirect test",
        "curl": f'--head http://127.0.0.1:{ats_port} -H "Host: no_path.com" --verbose',
        "gold": "gold/set-redirect.gold",
    },
    {
        "desc": "CLIENT-URL test",
        "curl": f'{curl_opt} "http://{url_base}_1/hello?=foo=bar"',
        "gold": "gold/client-url.gold",
    },
    {
        "desc": "sets matching",
        "curl": f'{curl_opt} "http://{url_base}_1/hrw-sets.png" -H "X-Testing: foo,bar"',
        "gold": "gold/ext-sets.gold",
    },
    {
        "desc": "elif condition",
        "curl": f'{curl_opt} "http://{url_base}_1/hrw-sets.png" -H "X-Testing: elif"',
        "gold": "gold/cond-elif.gold",
    },
    {
        "desc": "cond method GET",
        "curl": f'{curl_opt} "http://{url_base}_2/"',
        "gold": "gold/cond_method.gold",
    },
    {
        "desc": "cond method DELETE",
        "curl": f'--request DELETE {curl_opt} "http://{url_base}_2/"',
        "gold": "gold/cond_method.gold",
    },
    {
        "desc": "End [L] #5423",
        "curl": f'{curl_opt} "http://{url_base}_3/"',
        "gold": "gold/l_value.gold",
    },
]

# Run all the tests
started = False
for test in test_runs:
    tr = Test.AddTestRun(test["desc"])
    if not started:
        tr.Processes.Default.StartBefore(server, ready=When.PortOpen(origin_port))
        tr.Processes.Default.StartBefore(ts)
        started = True
    if "curl" in test:
        tr.MakeCurlCommand(test["curl"], ts=ts)
    tr.Processes.Default.Streams.stderr = test["gold"]
    tr.StillRunningAfter = server
