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

from tools.uranium.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_uri_signing(urtest: UraniumTest) -> None:
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

    urtest.Summary = '''
    Test uri_signing plugin
    '''

    urtest.ContinueOnFail = False

    # Skip if plugins not present.
    urtest.SkipUnless(Condition.PluginExists('uri_signing.so'))

    server = urtest.MakeOriginServer("server")

    # Default origin test
    req_header = {
        "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    res_header = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    server.addResponse("sessionfile.log", req_header, res_header)

    # Test case for normal
    req_header = {
        "headers": "GET /someasset.ts HTTP/1.1\r\nHost: somehost\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }

    res_header = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "somebody",
    }

    server.addResponse("sessionfile.log", req_header, res_header)

    # Test case for crossdomain
    req_header = {
        "headers": "GET /crossdomain.xml HTTP/1.1\r\nHost: somehost\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }

    res_header = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "<crossdomain></crossdomain>",
    }

    server.addResponse("sessionfile.log", req_header, res_header)

    # http://user:password@host:port/path;params?query#fragment

    # Define default ATS
    ts = urtest.MakeATSProcess("ts", enable_cache=False)
    #ts = Test.MakeATSProcess("ts", "traffic_server_valgrind.sh")

    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'uri_signing|http',
            # 'proxy.config.plugin.dynamic_reload_mode': 0,
            # 'proxy.config.diags.debug.tags': 'uri_signing',
        })

    # Use unchanged incoming URL.
    ts.Disk.remap_config.AddLine(
        'map http://somehost/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
        ' @plugin=uri_signing.so @pparam={}/config.json'.format(urtest.RunDirectory))

    # Install configuration
    ts.Setup.CopyAs('config.json', urtest.RunDirectory)
    ts.Setup.CopyAs('run_sign.sh', urtest.RunDirectory)
    ts.Setup.CopyAs('signer.json', urtest.RunDirectory)
    #ts.Setup.CopyAs('traffic_server_valgrind.sh', Test.RunDirectory)

    curl_and_args = '-q -v -x localhost:{} '.format(ts.Variables.port)

    # 0 - reject unsigned request
    tr = urtest.AddTestRun("unsigned request")
    ps = tr.Processes.Default
    ps.StartBefore(ts)
    ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
    tr.MakeCurlCommand(curl_and_args + 'http://somehost/someasset.ts', ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 1 - accept a passthru request
    tr = urtest.AddTestRun("passthru request")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(curl_and_args + 'http://somehost/crossdomain.xml', ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 2 - good token, signed "forever" (run_sign.sh 0)
    tr = urtest.AddTestRun("good signed")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 3 - expired token (run_sign.sh 1)
    tr = urtest.AddTestRun("expired signed")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 4 - good token, different key (run_sign.sh 2)
    tr = urtest.AddTestRun("good token, second key")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.ozH4sNwgcOlTZT0l4RQlVCH_osxz9yI1HCBesEv-jYg"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 5 - good token, inline
    tr = urtest.AddTestRun("good signed")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY/someasset.ts"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 6 - expired token, inline
    tr = urtest.AddTestRun("expired signed")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8/someasset.ts"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 7 - good token, param
    tr = urtest.AddTestRun("good signed, param")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 8 - expired token, param
    tr = urtest.AddTestRun("expired signed, param")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 9 - let's cookie this
    tr = urtest.AddTestRun("good signed cookie")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 10 - expired cookie token
    tr = urtest.AddTestRun("expired signed cooked")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 11 - multiple cookies
    tr = urtest.AddTestRun("multiple cookies, expired then good")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/200.gold"
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts

    # 12 - Check missing iss from the payload
    tr = urtest.AddTestRun("Missing iss field in the payload")
    ps = tr.Processes.Default
    tr.MakeCurlCommand(
        curl_and_args +
        '"http://somehost/someasset.ts?URISigningPackage=ewogICJ0eXAiOiAiSldUIiwKICAiYWxnIjogIkhTMjU2Igp9.ewogICJleHAiOiAxOTIzMDU2MDg0Cn0.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"',
        ts=ts)
    ps.ReturnCode = 0
    ps.Streams.stderr = "gold/403.gold"
    ts.Disk.traffic_out.Content = Testers.ContainsExpression(
        "Initial JWT Failure: iss is missing, must be present", "should fail the validation")
    tr.StillRunningAfter = server
    tr.StillRunningAfter = ts
    urtest.execute()
