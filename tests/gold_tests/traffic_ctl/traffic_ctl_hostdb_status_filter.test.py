'''
Verify that filtering hostdb status by HOSTNAME keeps the records array.
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

import json

from jsonrpc import Request, Response

Test.Summary = '''
Every partition in hostdb status output carries a `records` array, including a
partition whose records are all excluded by the HOSTNAME filter.

The filter can only empty a partition that holds records, and an empty HostDB
has no partitions to show, so HostDB is populated through DNS first. That is
why traffic_ctl_json_null.test.py, which needs an empty HostDB, cannot cover
this.
'''

Test.ContinueOnFail = True

# The unfiltered answer of each ATS process, as {partition id: sorted record
# names}, recorded by its first query. A filter changes what each partition
# holds, never which partitions are listed, so every filtered answer is checked
# against it. A stream tester runs as soon as its test run's process exits, so
# this is filled in before the next test run starts.
unfiltered = {}


def partition_names(partitions: object) -> tuple[dict | None, str]:
    '''Map each partition id to the sorted names of its records.

    Every partition has to be an object with an `id` no other partition has,
    and a `records` array. Returns (None, reason) when one is not.
    '''
    if not isinstance(partitions, list) or not partitions:
        return (None, f'partitions should be a non-empty array, got {partitions!r}')
    names = {}
    for partition in partitions:
        if not isinstance(partition, dict) or 'id' not in partition:
            return (None, f'partition carries no id: {partition!r}')
        if not isinstance(partition.get('records'), list):
            return (None, f'partition carries no records array: {partition!r}')
        if partition['id'] in names:
            return (None, f'partition {partition["id"]} is listed twice')
        names[partition['id']] = sorted(record['metadata']['name'] for record in partition['records'])
    return (names, '')


def check_unfiltered(ts: str, partitions: object, expected_names: list[str], expected_ids: list[str] | None) -> tuple[bool, str]:
    '''Check the unfiltered answer of `ts`, and keep it for its filtered checks.

    `expected_ids`, when given, are the partition ids the answer has to list.
    '''
    names, reason = partition_names(partitions)
    if names is None:
        return (False, reason)
    if not all(names.values()):
        return (False, f'unfiltered output lists an empty partition: {names}')
    found = {name for records in names.values() for name in records}
    if found != set(expected_names):
        return (False, f'records named {sorted(found)}, expected {sorted(expected_names)}')
    if expected_ids is not None and sorted(names) != sorted(expected_ids):
        return (False, f'partitions {sorted(names)} listed, expected {sorted(expected_ids)}')
    unfiltered[ts] = names
    return (True, f'{len(names)} partition(s): {names}')


def check_filtered(ts: str, partitions: object, hostname: str) -> tuple[bool, str]:
    '''Check a filtered answer of `ts` against its unfiltered one.

    The same partitions have to be listed, each holding exactly its unfiltered
    records whose names contain `hostname`, so a partition whose records all
    fail the filter carries an empty `records` array.
    '''
    names, reason = partition_names(partitions)
    if names is None:
        return (False, reason)
    if ts not in unfiltered:
        return (False, f'no unfiltered answer to compare against; got {names}')
    expected = {pid: [name for name in records if hostname in name] for pid, records in unfiltered[ts].items()}
    if names != expected:
        return (False, f'got {names}, expected {expected}')
    return (True, f'{len(names)} partition(s): {names}')


class HostDBStatusFilterTest:
    '''Populate HostDB with two names, then query it with and without a filter.

    One ATS process keeps the default 64 partitions, where the two names
    usually land in partitions of their own; the hash that places them
    includes the origin port. The other has a single partition, which both
    names share and whose id has to be 0.
    '''

    replay_file = 'replay/hostdb_status_filter.replay.yaml'

    # Neither name contains the other, so a filter on one excludes the other.
    one = 'one.hostdb.test'
    two = 'two.hostdb.test'

    def __init__(self) -> None:
        self._server = Test.MakeVerifierServerProcess('server', self.replay_file)

        self._dns = Test.MakeDNServer('dns')
        self._dns.addRecords(records={self.one: ['127.0.0.1'], self.two: ['127.0.0.1']})

        self._ts = self._make_ts('ts', {})
        self._ts1 = self._make_ts('ts1', {'proxy.config.hostdb.partitions': 1})

    def _make_ts(self, name: str, records: dict) -> 'Process':
        ts = Test.MakeATSProcess(name, enable_cache=False)
        ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                **records,
            })
        for host in (self.one, self.two):
            ts.Disk.remap_config.AddLine(f'map http://{host}/ http://{host}:{self._server.Variables.http_port}/')
        return ts

    def _populate_hostdb(self) -> None:
        # The DNS and origin servers serve both ATS processes. Only the first
        # run starts them: starting a running process again repeats its setup,
        # which fails.
        for ts in (self._ts, self._ts1):
            tr = Test.AddTestRun(f'Resolve both origins through {ts.Name}, which puts both names into its HostDB')
            tr.AddVerifierClientProcess(f'client_{ts.Name}', self.replay_file, http_ports=[ts.Variables.port])
            if ts is self._ts:
                tr.Processes.Default.StartBefore(self._dns)
                tr.Processes.Default.StartBefore(self._server)
            tr.Processes.Default.StartBefore(ts)
            tr.StillRunningAfter = ts

    def _rpc_status(self, ts: 'Process', description: str, hostname: str, expected_ids: list[str] | None = None) -> None:
        '''Check the get_hostdb_status response as the server sends it.'''

        def check(resp: Response) -> tuple[bool, str]:
            if resp.is_error():
                return (False, resp.error_as_str())
            partitions = resp.result['data']['partitions']
            if not hostname:
                return check_unfiltered(ts.Name, partitions, [self.one, self.two], expected_ids)
            return check_filtered(ts.Name, partitions, hostname)

        tr = Test.AddTestRun(description)
        tr.AddJsonRPCClientRequest(ts, Request.get_hostdb_status(hostname=hostname))
        tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(check)
        tr.StillRunningAfter = ts

    def _cli_status(self, ts: 'Process', description: str, hostname: str) -> None:
        '''Check what traffic_ctl hostdb status HOSTNAME prints.'''

        def check(info, tester) -> tuple[bool, str, str]:
            desc = f'Check traffic_ctl hostdb status {hostname}'
            with open(tester.GetContent(info), 'r') as stdout:
                raw = stdout.read()
            try:
                doc = json.loads(raw)
            except ValueError as ex:
                return (False, desc, f'Output is not JSON: {ex}\nOutput was:\n{raw}')
            passed, reason = check_filtered(ts.Name, doc.get('partitions'), hostname)
            return (passed, desc, f'{reason}\nOutput was:\n{raw}' if not passed else reason)

        tr = Test.AddTestRun(description)
        tr.Processes.Default.Command = f'traffic_ctl hostdb status {hostname}'
        tr.Processes.Default.Env = ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.Lambda(check)
        tr.StillRunningAfter = ts

    def run(self) -> None:
        self._populate_hostdb()

        # The baseline: without a filter, both names are there, and every
        # partition listed holds at least one record.
        self._rpc_status(self._ts, 'get_hostdb_status without a filter', '')

        # A filter matching one name. A partition holding only the other name
        # is still listed, with an empty `records` array. Whether the two names
        # share a partition depends on their hashes, so the no-match cases
        # below are the ones that empty a partition unconditionally.
        self._rpc_status(self._ts, 'get_hostdb_status filtered to one name', self.one)
        self._cli_status(self._ts, 'traffic_ctl hostdb status filtered to one name', self.one)

        # HOSTNAME matches any record whose name contains it, not only one
        # that equals it or starts with it.
        self._rpc_status(self._ts, 'get_hostdb_status filtered to part of one name', 'ne.hostdb')

        # A filter matching nothing. Every partition is still listed, each
        # with an empty `records` array.
        self._rpc_status(self._ts, 'get_hostdb_status filtered to no name', 'no.such.host')
        self._cli_status(self._ts, 'traffic_ctl hostdb status filtered to no name', 'no.such.host')

        # One partition holding both names. The filter has to keep one of its
        # records and drop the other, or drop both and leave `records` empty.
        self._rpc_status(self._ts1, 'get_hostdb_status of one partition without a filter', '', expected_ids=['0'])
        self._rpc_status(self._ts1, 'get_hostdb_status of one partition filtered to one name', self.one)
        self._rpc_status(self._ts1, 'get_hostdb_status of one partition filtered to no name', 'no.such.host')


HostDBStatusFilterTest().run()
