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
Origin TLS sessions must still be cached when the serialized session is large.

The origin session cache refuses to store a session whose i2d_SSL_SESSION form
exceeds SSL_MAX_ORIG_SESSION_SIZE.  A serialized session carries the peer
certificate and the session ticket, so an origin with a large certificate -- or
any mutual-TLS origin, whose ticket encodes the client certificate -- produces a
session that trips that limit and is silently dropped, costing a full handshake
on every connection.
'''

# ts_origin presents a deliberately large certificate (120 SANs, 4328 bytes DER),
# so the session ts_proxy caches for it does not fit in a small fixed buffer.
ts_origin = Test.MakeATSProcess("ts_origin", enable_tls=True)
ts_proxy = Test.MakeATSProcess("ts_proxy", enable_tls=True)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "large session test"
}
server.addResponse("sessionlog.json", request_header, response_header)

ts_origin.addSSLfile("ssl/server-large.pem")
ts_origin.addSSLfile("ssl/server-large.key")
ts_proxy.addSSLfile("ssl/server.pem")
ts_proxy.addSSLfile("ssl/server.key")

ts_origin.Disk.remap_config.AddLine("map / http://127.0.0.1:{0}".format(server.Variables.Port))
ts_proxy.Disk.remap_config.AddLine("map / https://127.0.0.1:{0}".format(ts_origin.Variables.ssl_port))

ts_origin.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server-large.pem
    ssl_key_name: server-large.key
""".split("\n"))
ts_proxy.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

ts_origin.Disk.records_config.update(
    {
        "proxy.config.http.cache.http": 0,
        "proxy.config.ssl.server.cert.path": "{0}".format(ts_origin.Variables.SSLDir),
        "proxy.config.ssl.server.private_key.path": "{0}".format(ts_origin.Variables.SSLDir),
        "proxy.config.exec_thread.autoconfig.scale": 1.0,
        "proxy.config.ssl.server.session_ticket.enable": 1,
    })

ts_proxy.Disk.records_config.update(
    {
        "proxy.config.http.cache.http": 0,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl.origin_session_cache",
        "proxy.config.ssl.server.cert.path": "{0}".format(ts_proxy.Variables.SSLDir),
        "proxy.config.ssl.server.private_key.path": "{0}".format(ts_proxy.Variables.SSLDir),
        "proxy.config.exec_thread.autoconfig.scale": 1.0,
        "proxy.config.ssl.origin_session_cache.enabled": 1,
        "proxy.config.ssl.origin_session_cache.size": 10,
        "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
    })

tr = Test.AddTestRun("large origin session is cached and reused")
tr.MakeCurlCommandMulti(
    "{{curl}} https://127.0.0.1:{0}/ -k && {{curl}} https://127.0.0.1:{0}/ -k".format(ts_proxy.Variables.ssl_port), ts=ts_proxy)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts_origin)
tr.Processes.Default.StartBefore(ts_proxy)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("large session test", "the request itself has to work")
ts_proxy.Disk.traffic_out.Content = Testers.ContainsExpression("new session to origin", "the first connection is a full handshake")
ts_proxy.Disk.traffic_out.Content += Testers.ContainsExpression(
    "reused session to origin", "the second connection must resume, which requires the large session to have been cached")
ts_proxy.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "Unable to save SSL session because size", "a valid origin session must not be dropped for its size")
tr.StillRunningAfter = server
tr.StillRunningAfter += ts_origin
tr.StillRunningAfter += ts_proxy

# The same large session, against a proxy configured with the old fixed ceiling.
# This is the regression: a session that is perfectly valid gets dropped for its
# size, and every connection pays a full handshake.
ts_small = Test.MakeATSProcess("ts_small", enable_tls=True)
ts_small.addSSLfile("ssl/server.pem")
ts_small.addSSLfile("ssl/server.key")
ts_small.Disk.remap_config.AddLine("map / https://127.0.0.1:{0}".format(ts_origin.Variables.ssl_port))
ts_small.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
ts_small.Disk.records_config.update(
    {
        "proxy.config.http.cache.http": 0,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl.origin_session_cache",
        "proxy.config.ssl.server.cert.path": "{0}".format(ts_small.Variables.SSLDir),
        "proxy.config.ssl.server.private_key.path": "{0}".format(ts_small.Variables.SSLDir),
        "proxy.config.exec_thread.autoconfig.scale": 1.0,
        "proxy.config.ssl.origin_session_cache.enabled": 1,
        "proxy.config.ssl.origin_session_cache.size": 10,
        "proxy.config.ssl.origin_session_cache.max_session_size": 4096,
        "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
    })

tr = Test.AddTestRun("a ceiling below the session size drops it, as configured")
tr.MakeCurlCommandMulti(
    "{{curl}} https://127.0.0.1:{0}/ -k && {{curl}} https://127.0.0.1:{0}/ -k".format(ts_small.Variables.ssl_port), ts=ts_small)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts_small)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("large session test", "the request still succeeds")
ts_small.Disk.traffic_out.Content = Testers.ContainsExpression(
    "Unable to save SSL session because size", "the session is over this proxy's configured ceiling")
ts_small.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "reused session to origin", "nothing was cached, so nothing can resume")
tr.StillRunningAfter = server
tr.StillRunningAfter += ts_origin
