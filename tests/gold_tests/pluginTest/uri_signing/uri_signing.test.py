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

import os
import subprocess
Test.Summary = '''
Test uri_signing plugin
'''

Test.ContinueOnFail = False

# Skip if plugins not present.
Test.SkipUnless(Condition.PluginExists('uri_signing.so'))
Test.SkipUnless(Condition.PluginExists('cachekey.so'))

server = Test.MakeOriginServer("server")

# Default origin test
req_header = { "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "",
}
res_header = { "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "",
}
server.addResponse("sessionfile.log", req_header, res_header)

# Test case for normal
req_header = { "headers":
	"GET /someasset.ts HTTP/1.1\r\nHost: somehost\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "",
}

res_header = { "headers":
	"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "somebody",
}

server.addResponse("sessionfile.log", req_header, res_header)

# Test case for crossdomain
req_header = { "headers":
	"GET /crossdomain.xml HTTP/1.1\r\nHost: somehost\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "",
}

res_header = { "headers":
	"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
	"timestamp": "1469733493.993",
	"body": "<crossdomain></crossdomain>",
}

server.addResponse("sessionfile.log", req_header, res_header)

# http://user:password@host:port/path;params?query#fragment

# Define default ATS
ts = Test.MakeATSProcess("ts")
#ts = Test.MakeATSProcess("ts", "traffic_server_valgrind.sh")

ts.Disk.records_config.update({
  'proxy.config.diags.debug.enabled': 1,
  'proxy.config.diags.debug.tags': 'uri_signing|http',
#  'proxy.config.diags.debug.tags': 'uri_signing',
  'proxy.config.http.cache.http': 0,  # No cache
})

# Use unchanged incoming URL.
# This uses cachekey to handle the effective vs pristine url diff for the
# first plugin issue that exists in 8.x. This is not necessary on 9x+
ts.Disk.remap_config.AddLine(
    'map http://somehost/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=cachekey.so @pparam=--include-headers=foo' +
    ' @plugin=uri_signing.so @pparam={}/config.json'.format(Test.RunDirectory)
)

# Install configuration
ts.Setup.CopyAs('config.json', Test.RunDirectory)
ts.Setup.CopyAs('run_sign.sh', Test.RunDirectory)
ts.Setup.CopyAs('signer.json', Test.RunDirectory)
#ts.Setup.CopyAs('traffic_server_valgrind.sh', Test.RunDirectory)

curl_and_args = 'curl -q -v -x localhost:{} '.format(ts.Variables.port)

# 0 - reject unsigned request
tr = Test.AddTestRun("unsigned request")
ps = tr.Processes.Default
ps.StartBefore(ts)
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.Command = curl_and_args + 'http://somehost/someasset.ts'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/403.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 1 - accept a passthru request
tr = Test.AddTestRun("passthru request")
ps = tr.Processes.Default
ps.Command = curl_and_args + 'http://somehost/crossdomain.xml'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 2 - good token, signed "forever" (run_sign.sh 0)
tr = Test.AddTestRun("good signed")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 3 - expired token (run_sign.sh 1)
tr = Test.AddTestRun("expired signed")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/403.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 4 - good token, different key (run_sign.sh 2)
tr = Test.AddTestRun("good token, second key")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts?URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.ozH4sNwgcOlTZT0l4RQlVCH_osxz9yI1HCBesEv-jYg"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 5 - good token, inline
tr = Test.AddTestRun("good signed")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY/someasset.ts"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 6 - expired token, inline
tr = Test.AddTestRun("expired signed")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8/someasset.ts"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/403.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 7 - good token, param
tr = Test.AddTestRun("good signed, param")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 8 - expired token, param
tr = Test.AddTestRun("expired signed, param")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/403.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 9 - let's cookie this
tr = Test.AddTestRun("good signed cookie")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 10 - expired cookie token
tr = Test.AddTestRun("expired signed cooked")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/403.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# 9 - multiple cookies
tr = Test.AddTestRun("multiple cookies, expired then good")
ps = tr.Processes.Default
ps.Command = curl_and_args + '"http://somehost/someasset.ts" -H "Cookie: URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjF9.GkdlOPHQc6BqS4Q6x79GeYuVFO2zuGbaPZZsJfD6ir8;URISigningPackage=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJpc3N1ZXIiLCJleHAiOjE5MjMwNTYwODR9.zw_wFQ-wvrWmfPLGj3hAUWn-GOHkiJZi2but4KV0paY"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
