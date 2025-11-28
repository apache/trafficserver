'''Verify cache directory SystemTap probes fire on insert and remove.'''
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

Test.Summary = '''Verify cache directory SystemTap probes fire on cache fill and PURGE.'''

# Skipping this test generally because it requires privilege. Thus most CI systems will skip it.
Test.SkipUnless(
    Condition(lambda: os.geteuid() == 0, "Test requires privilege", True),
    Condition.HasProgram("bpftrace", "Need bpftrace to verify the probe."))


class CacheDirProbeTest:
    '''Verify cache directory SystemTap probes.'''
    bt_script: str = 'cache_dir_probe.bt'
    _cache_path: str = '/cacheable'

    def __init__(self):
        tr = Test.AddTestRun('Cache directory probes should trigger on insert and purge.')
        self._configure_origin(tr)
        self._configure_traffic_server(tr)
        self._configure_bpftrace(tr)
        self._configure_client(tr)

    def _configure_origin(self, tr: 'TestRun') -> 'Process':
        '''Configure the origin microserver.'''
        origin = Test.MakeOriginServer('origin')
        self._origin = origin

        cache_request = {
            "headers": f"GET {self._cache_path} HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        cache_response = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=120\r\nContent-Length: 5\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "hello"
        }
        origin.addResponse("sessionlog.json", cache_request, cache_response)
        origin.addResponse("sessionlog.json", cache_request, cache_response)

        purge_request = {
            "headers": f"PURGE {self._cache_path} HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        purge_response = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        origin.addResponse("sessionlog.json", purge_request, purge_response)
        return origin

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        '''Configure the Traffic Server process.'''
        ts = tr.MakeATSProcess("ts_cache_dir_probe", enable_cache=True)
        self._ts = ts
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|cache',
                'proxy.config.http.cache.required_headers': 0,
                # Keep ATS running as the invoking user inside sudo (no privilege drop).
                'proxy.config.admin.user_id': '#-1',
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}')
        return ts

    def _configure_bpftrace(self, tr: 'TestRun') -> 'Process':
        '''Configure the bpftrace process for the cache directory probes.'''
        bpftrace = tr.Processes.Process('bpftrace')
        self._bpftrace = bpftrace

        tr.Setup.Copy(self.bt_script)
        tr_script = os.path.join(tr.RunDirectory, self.bt_script)

        # fan out output so AuTest stream checks still work
        tee_path = os.path.join(tr.RunDirectory, 'bpftrace.out')
        bpftrace.Command = f"bpftrace {tr_script}"
        bpftrace.ReturnCode = 0
        bpftrace.Streams.All += Testers.ContainsExpression('cache_dir_insert', 'cache_dir_insert probe fired.')
        bpftrace.Streams.All += Testers.ContainsExpression('cache_dir_remove', 'cache_dir_remove probe fired.')

        return bpftrace

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure the client traffic to exercise cache insert and purge.'''
        client = tr.Processes.Default
        self._client = client
        cache_url = f"http://127.0.0.1:{self._ts.Variables.port}{self._cache_path}"
        # Ideally we don't need this "sleep 1", but I haven't been able to get it to work with the Ready = When.FileContains(...) approach.
        client.Command = (
            f"sleep 1 && curl -sSf -o /dev/null -H 'Host: cache-probe.test' {cache_url} && "
            f"curl -sSf -o /dev/null -X PURGE -H 'Host: cache-probe.test' {cache_url} && "
            f"curl -sSf -o /dev/null -H 'Host: cache-probe.test' {cache_url}")
        client.ReturnCode = 0
        client.Env = self._ts.Env

        self._ts.StartBefore(self._origin)  # origin before ts
        self._bpftrace.StartBefore(self._ts)  # ts before bpftrace
        client.StartBefore(self._bpftrace)  # bpftrace before client
        return client


CacheDirProbeTest()
