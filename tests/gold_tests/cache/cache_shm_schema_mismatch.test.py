'''
Verify the shm schema-version trust gate: a control segment whose schema_version
does not match the running build is dropped, never fast-attached. ts1 cold-starts,
caches an object, and clean-shuts-down (marking the segment clean). The segment
file under /dev/shm is then tampered -- schema_version is overwritten with a
bogus value -- before ts2 starts against the same shm prefix. ts2 must detect the
mismatch, drop the segment, recreate it fresh, and rebuild the directory from disk.

Linux-only: it pokes raw bytes in the /dev/shm segment file, which exists only on
Linux (macOS POSIX shm segments are not path-addressable).
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
import platform
import sys
import uuid

Test.Summary = '''
A control segment with a mismatched schema_version is dropped and rebuilt from
disk, never fast-attached.
'''
Test.ContinueOnFail = True

# The byte-poke drives the gate by editing /dev/shm directly, which is a Linux
# facility; macOS POSIX shm is not exposed as a file. There is no Condition for
# the platform, so gate with a lambda (ports.py branches on platform the same way).
Test.SkipUnless(Condition(lambda: platform.system() == 'Linux', "shm byte-poke gates need Linux /dev/shm"))


class CacheShmSchemaMismatchTest:
    """
    The schema-version gate. The control header records the build's
    CACHE_SHM_SCHEMA_VERSION; on attach, a segment whose recorded version differs
    is dropped ("schema mismatch (<got> vs <want>), dropping") rather than trusted
    -- the on-disk struct layout it describes may no longer match this build. The
    ABI-hash gate (abi_hash @16) works identically; this test exercises the
    schema field (@8) as the representative case.

    Sequence: ts1 creates a clean segment, then schema_version is poked to a bogus
    value, then ts2 starts and must:
      - log the schema mismatch and drop,
      - recreate a fresh control segment,
      - NOT fast-attach,
      - and still serve a request (200).
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'
    POKE_SCRIPT = 'shm_poke.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    # CacheShmControl layout (CacheShmLayout.h): magic[8] @0, schema_version @8.
    SCHEMA_VERSION_OFFSET = 8
    # Little-endian uint32 = 9; the build's CACHE_SHM_SCHEMA_VERSION is small, so
    # any value it never uses works. 9 is comfortably out of range.
    BOGUS_SCHEMA_LE_HEX = '09000000'

    def __init__(self):
        self._setup_shared_state()
        self.ts1 = self._configure_ts('shmx_ts1')
        self.ts2 = self._configure_ts('shmx_ts2')
        self._add_diags_log_assertions()
        self._url_path = f'/cache/40/{uuid.uuid4()}'

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))
        Test.Setup.Copy(os.path.join(Test.TestDirectory, self.POKE_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        self._shared_storage_path = os.path.join(shared_storage_dir, 'disk.img')
        with open(self._shared_storage_path, 'ab') as f:
            f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'x' = schema-mismatch variant.
        # (This test is Linux-only, but keep the prefix short for consistency.)
        self._shm_prefix = f'/cshmx-{os.getpid() % 100000}-'
        # The control segment is name_prefix + "control"; on Linux it is a file
        # under /dev/shm by the same name (sans the leading '/').
        self._control_file = '/dev/shm/' + self._shm_prefix.lstrip('/') + 'control'

    def _configure_ts(self, name):
        ts = Test.MakeATSProcess(name)
        ts.Disk.storage_yaml.AddLines(
            [
                'cache:',
                '  spans:',
                '    - name: disk.0',
                f'      path: {self._shared_storage_path}',
                f'      size: {self.SHARED_DISK_SIZE_BYTES}',
                '  volumes:',
                '    - id: 1',
                '      scheme: http',
                '      size: 100%',
            ])
        ts.Disk.records_config.update(
            {
                'proxy.config.cache.shm.enabled': 1,
                'proxy.config.cache.shm.name_prefix': self._shm_prefix,
                'proxy.config.cache.shm.use_hugepages': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cache_shm',
                'proxy.config.diags.output.diag': 'L',
                'proxy.config.http.wait_for_cache': 1,
            })
        ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,via')
        ts.Disk.remap_config.AddLine('map / http://127.0.0.1/ @plugin=generator.so')
        return ts

    def _add_diags_log_assertions(self):
        # ts1 cold start, clean shutdown -- a valid, clean segment to tamper with.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')

        # ts2 start against the poked segment: detect, drop, recreate, rebuild.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: schema mismatch \(\d+ vs \d+\), dropping', 'ts2 must detect the schema mismatch and drop the segment')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts2 must recreate the control segment after the drop')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'\(fast restart, recovery skipped\)', 'ts2 must rebuild from disk, never fast-attach the mismatched segment')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly',
            'the drop must be due to the schema mismatch, not an unclean shutdown')

    def _populate_cache(self):
        tr = Test.AddTestRun('Cold-start ts1 and cache an object')
        tr.Processes.Default.StartBefore(self.ts1)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts1.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('200', 'ts1 first GET should return 200')
        tr.StillRunningAfter = self.ts1

    def _clean_shutdown_ts1(self):
        tr = Test.AddTestRun('Drain and clean-shutdown ts1')
        tr.Processes.Default.Env = self.ts1.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shmx_ts1 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _poke_schema_version(self):
        # ts1 is dead; the segment is just a file now. Overwrite schema_version.
        tr = Test.AddTestRun('Tamper schema_version in the shm control segment')
        tr.Processes.Default.Command = (
            f'{sys.executable} ./{self.POKE_SCRIPT} {self._control_file} '
            f'{self.SCHEMA_VERSION_OFFSET} {self.BOGUS_SCHEMA_LE_HEX}')
        tr.Processes.Default.ReturnCode = 0

    def _verify_mismatch_drop(self):
        tr = Test.AddTestRun('Start ts2; verify the schema mismatch is dropped and rebuilt from disk')
        tr.Processes.Default.StartBefore(self.ts2)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts2.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            '200', 'ts2 should serve correctly after dropping the mismatched segment')
        tr.StillRunningAfter = self.ts2

    def _clean_shutdown_ts2(self):
        tr = Test.AddTestRun('Drain and clean-shutdown ts2')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shmx_ts2 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _cleanup_shm(self):
        tr = Test.AddTestRun('Unlink the test shm segments')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm clear --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
            'Invalid argument', 'clear must skip tombstoned slots, not fail on them')

    def run(self):
        self._populate_cache()
        self._clean_shutdown_ts1()
        self._poke_schema_version()
        self._verify_mismatch_drop()
        self._clean_shutdown_ts2()
        self._cleanup_shm()


CacheShmSchemaMismatchTest().run()
