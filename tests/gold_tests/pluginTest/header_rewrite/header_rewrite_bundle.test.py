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
Test.ContinueOnFail = True

# Setup ATS and origin, define some convenience variables
mgr = ats_autest.ATSTestManager(Test, When)

# Common debug logging config
mgr.enable_diagnostics(tags="header_rewrite")
mgr.ts.Disk.records_config.update(
    {
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.http.auth_server_session_private': 1,
        'proxy.config.http.server_session_sharing.pool': 'global',
        'proxy.config.http.server_session_sharing.match': 'both'
    })

# mgr.ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"

#############################################################################
# Setup all the remap rules
#
url_base = "www.example.com/from"
origin_base = f'127.0.0.1:{mgr.origin_port}/to'

remap_rules = [
    {
        "from": "no_path.com/",
        "to": "no_path.com?name=brian/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/set_redirect.conf"])]
    }, {
        "from": f"{url_base}_1/",
        "to": f"{origin_base}_1/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_client.conf"])]
    }, {
        "from": f"{url_base}_2/",
        "to": f"{origin_base}_2/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_cond_method.conf"])]
    }, {
        "from": f"{url_base}_3/",
        "to": f"{origin_base}_3/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_l_value.conf"])]
    }, {
        "from": f"{url_base}_4/",
        "to": f"{origin_base}_4/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_set_header_after_ssn_txn_count.conf"])]
    }, {
        "from": f"{url_base}_5/",
        "to": f"{origin_base}_5/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_add_cache_result_header.conf"])]
    }, {
        "from": f"{url_base}_6/",
        "to": f"{origin_base}_6/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule_effective_address.conf"])]
    }, {
        "from": f"{url_base}_7/",
        "to": f"{origin_base}_7/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/rule.conf"])]
    }, {
        "from": f"{url_base}_8/",
        "to": f"{origin_base}_8/",
        "plugins": [("header_rewrite", [f"{mgr.run_dir}/implicit_hook.conf"])]
    }
]

mgr.copy_files('rules/', pattern='*.conf')
mgr.add_remap_rules(remap_rules)

#############################################################################
# Setup the origin server rquest/response pairs
#
def_resp = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
origin_rules = [
    ({
        "headers": "GET / HTTP/1.1\r\nHost: no_path.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }, def_resp),
    (
        {
            "headers": "GET /to_1/hello?=foo=bar HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "",
        }, def_resp),
    (
        {
            "headers": "GET /to_1/hrw-sets.png HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }, def_resp),
    ({
        "headers": "GET /to_2/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, def_resp),
    ({
        "headers": "GET /to_3/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, def_resp),
    (
        {
            "headers": "GET /to_4/hello HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }, {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                       "Content-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }),
    (
        {
            "headers": "GET /to_4/world HTTP/1.1\r\nContent-Length: 0\r\n"
                       "Host: www.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "a\r\na\r\na\r\n\r\n"
        }, {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                       "Connection: close\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }),
    (
        {
            "headers": "GET /to_5/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=5,public\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "CACHED"
        }),
    ({
        "headers": "GET /to_6/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, def_resp),
    ({
        "headers": "GET /to_7/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, def_resp),
    ({
        "headers": "GET /to_8/ HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, def_resp),
]
mgr.add_server_responses(origin_rules)

# Create all the test cases
curl_proxy = f'--proxy {mgr.localhost} --verbose'
expected_log = "gold/header_rewrite-tag.gold"

test_runs = [
    {
        "desc": "Setup cache hit for tests later (cache hit /miss)",
        "curl": f'-s -v -H "{mgr.host_example}" {mgr.localhost}/from_5/',
        "gold": "gold/cond_cache_first.gold",
    },
    {
        "desc": "TO-URL redirect test",
        "curl": f'--head http://{mgr.localhost} -H "Host: no_path.com" --verbose',
        "gold": "gold/set-redirect.gold",
    },
    {
        "desc": "CLIENT-URL test",
        "curl": f'{curl_proxy} "http://{url_base}_1/hello?=foo=bar"',
        "gold": "gold/client-url.gold",
    },
    {
        "desc": "sets matching",
        "curl": f'{curl_proxy} "http://{url_base}_1/hrw-sets.png" -H "X-Testing: foo,bar"',
        "gold": "gold/ext-sets.gold",
    },
    {
        "desc": "elif condition",
        "curl": f'{curl_proxy} "http://{url_base}_1/hrw-sets.png" -H "X-Testing: elif"',
        "gold": "gold/cond-elif.gold",
    },
    {
        "desc": "cond method GET",
        "curl": f'{curl_proxy} "http://{url_base}_2/"',
        "gold": "gold/cond_method.gold",
    },
    {
        "desc": "cond method DELETE",
        "curl": f'--request DELETE {curl_proxy} "http://{url_base}_2/"',
        "gold": "gold/cond_method.gold",
    },
    {
        "desc": "End [L] #5423",
        "curl": f'{curl_proxy} "http://{url_base}_3/"',
        "gold": "gold/l_value.gold",
    },
    {
        "desc": "SSN-TXN-COUNT condition",

        # Force last one with close connection header, this is also reflected in the response ^.
        # if I do not do this, then the microserver will fail to close and when shutting down the process will
        # fail with -9.
        "multi_curl":
            (
                f'{{curl}} -v -H "{mgr.host_example}" -H "{mgr.conn_keepalive}" {mgr.localhost}/from_4/hello &&'
                f'{{curl}} -v -H "{mgr.host_example}" -H "{mgr.conn_keepalive}" {mgr.localhost}/from_4/hello &&'
                f'{{curl}} -v -H "{mgr.host_example}" -H "{mgr.conn_keepalive}" {mgr.localhost}/from_4/hello &&'
                f'{{curl}} -v -H "{mgr.host_example}" -H "{mgr.conn_keepalive}" {mgr.localhost}/from_4/hello &&'
                f'{{curl}} -v -H "{mgr.host_example}" -H "Connection: close" {mgr.localhost}/from_4/world'),
        "gold": "gold/cond_ssn_txn_count.gold"
    },
    {
        "desc": "Cache condition test - miss, hit-fresh, hit-stale, hit-fresh",
        "multi_curl":
            (
                f'{{curl}} -s -v -H "{mgr.host_example}" {mgr.localhost}/from_5/ && '
                f'sleep 8 && {{curl}} -s -v -H "{mgr.host_example}" {mgr.localhost}/from_5/ && '
                f'{{curl}} -s -v -H "{mgr.host_example}" {mgr.localhost}/from_5/'),
        "gold": "gold/cond_cache.gold",
    },
    {
        "desc": "Effective address test",
        "curl": f'{curl_proxy} "http://{url_base}_6/" -H "Real-IP: 1.2.3.4"',
        "gold": "gold/header_rewrite_effective_address.gold",
    },
    {
        "desc": "Status change test (200 to 303)",
        "curl": f'{curl_proxy} "http://{url_base}_7/"',
        "gold": "gold/header_rewrite-303.gold",
    },
    {
        "desc": "Implicit hook test - no X-Fie header (expect X-Response-Foo: No)",
        "curl": f'{curl_proxy} "http://{url_base}_8/"',
        "gold": "gold/implicit_hook_no_fie.gold",
    },
    {
        "desc": "Implicit hook test - X-Fie: Fie (expect X-Response-Foo: Yes)",
        "curl": f'{curl_proxy} "http://{url_base}_8/" -H "X-Fie: Fie"',
        "gold": "gold/implicit_hook_fie.gold",
    },
    {
        "desc": "Implicit hook test - X-Client-Foo: fOoBar (expect X-Response-Foo: Prefix)",
        "curl": f'{curl_proxy} "http://{url_base}_8/" -H "X-Client-Foo: fOoBar"',
        "gold": "gold/implicit_hook_prefix.gold",
    },
]

mgr.execute_tests(test_runs)
