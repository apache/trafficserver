'''
A cached object written against a different well-known string table is still served correctly.

A cached object stores indexes into the well-known string table (the field index of every MIME
field, the request method's, the request URL scheme's) right next to the strings those indexes
stand for. Change the table -- add, remove or reorder a string -- and every one of those indexes
denotes a different string than it did when the object was written. HTTPInfo::unmarshal() therefore
rebuilds all of them, plus the presence bits and slot accelerators derived from them, from the
strings the object carries.

The table is built at compile time, so a process cannot host two of them. This test stands in for
the second table with ATS_TEST_WKS_IDX_SHIFT, which makes marshalling rotate every index it writes
and clear the derived bits -- strictly worse than any real table change, since a real one leaves
the indexes self-consistent for the writer's table.

Emitting a cached response does not on its own prove the rebuild happened, because the response is
printed from the stored strings and comes out right either way. The transactions in the replay file
instead turn on ATS *finding* particular fields in the cached header, which is what the rebuilt
indexes and presence bits are for. The sharpest of them is the revalidation: without the rebuild
ATS cannot find Etag or Last-Modified in the cached response, so it sends a plain GET where it owes
the origin a conditional one.
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

Test.Summary = 'A cache written against a different well-known string table is still served correctly'

# ATS_TEST_WKS_IDX_SHIFT is compiled in only when TS_HAS_TESTS is enabled. Without it the shift is
# a no-op and this test would exercise an ordinary cache hit instead of a table change.
Test.SkipUnless(Condition.HasATSFeature('TS_HAS_TESTS'))

Test.ContinueOnFail = True

REPLAY_FILE = 'replay/cache-wks-table-change.replay.yaml'

server = Test.MakeVerifierServerProcess('wks-origin', REPLAY_FILE)

ts = Test.MakeATSProcess('ts', enable_cache=True)
# Rotate every well-known string index written into a cached object. 7 is arbitrary; any non-zero
# value that is not a multiple of the table size moves every index onto some other string.
ts.Env['ATS_TEST_WKS_IDX_SHIFT'] = '7'

ts.Disk.records_config.update(
    {
        'proxy.config.http.wait_for_cache': 1,
        # The RAM cache holds the object already unmarshalled, so a hit on it would never read the
        # rotated indexes back. Turn it off to force the read through the marshalled bytes.
        'proxy.config.cache.ram_cache.size': 0,
    })
ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,via')
ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.http_port}/')

tr = Test.AddTestRun('Cache an object written with a rotated well-known string table, then hit it')
tr.AddVerifierClientProcess('wks-client', REPLAY_FILE, http_ports=[ts.Variables.port], other_args='--thread-limit 1')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
