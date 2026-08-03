'''
Verify a control segment whose size is not this build's sizeof(CacheShmControl) is
dropped and recreated rather than wedging shm off. Such a segment is what an upgrade
that changes the control layout (a MAX_STRIPES bump, a longer shm_name, a new
StripeEntry field) leaves behind: the size-checked attach cannot map it, and without
the frozen-header drop path the O_EXCL create would then fail with EEXIST on every
restart until an operator ran `traffic_ctl cache shm clear`.

ts1 cold-starts, caches an object, and clean-shuts-down. The control segment file
under /dev/shm is then grown past any size this build could have written, so ts2 sees
a foreign-size segment: it must drop it, create a fresh one, and rebuild from disk.
ts3 then starts against what ts2 left behind and must fast-attach normally, which is
what proves the drop actually healed rather than deferring the wedge by one restart.

Linux-only: it resizes the /dev/shm segment file, which exists only on Linux (macOS
POSIX shm segments are not path-addressable).
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
A control segment written with a different sizeof(CacheShmControl) is dropped and
recreated, never left to wedge the create path.
'''
Test.ContinueOnFail = True

# The poke drives the gate by editing /dev/shm directly, which is a Linux facility;
# macOS POSIX shm is not exposed as a file. There is no Condition for the platform,
# so gate with a lambda (ports.py branches on platform the same way).
Test.SkipUnless(Condition(lambda: platform.system() == 'Linux', "shm byte-poke gates need Linux /dev/shm"))


class CacheShmControlSizeMismatchTest:
    """
    The control-segment size gate. Every field the attach path needs to identify a
    segment (magic, schema_version, abi_hash) and to guard dropping it (owner_pid,
    clean_shutdown) lives in a frozen prefix, so a segment of any other size is still
    readable that far: it is guarded, dropped, and recreated in one start.

    Sequence: ts1 creates a clean segment; the segment file is grown past this build's
    size; ts2 must report the size mismatch, drop, recreate, and serve; ts3 must then
    fast-attach the segment ts2 created.
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'
    POKE_SCRIPT = 'shm_poke.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    # Writing one byte at this offset grows the (sparse) segment file well past
    # sizeof(CacheShmControl) rounded up to a page, which is what the attach path
    # accepts. MAX_CONTROL_SEGMENT_BYTES caps the struct at 32 KiB, so 1 MiB is
    # beyond reach of any layout this build could compile. The header itself is left
    # intact, so the size is the only thing that makes the segment foreign.
    GROW_OFFSET = 1024 * 1024
    GROW_BYTE_HEX = '00'

    def __init__(self):
        self._setup_shared_state()
        self.ts1 = self._configure_ts('shmz_ts1')
        self.ts2 = self._configure_ts('shmz_ts2')
        self.ts3 = self._configure_ts('shmz_ts3')
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

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'z' = size-mismatch variant.
        # (This test is Linux-only, but keep the prefix short for consistency.)
        self._shm_prefix = f'/cshmz-{os.getpid() % 100000}-'
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
        # ts1 cold start, clean shutdown -- a valid, clean segment to resize.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')

        # ts2 start against the resized segment: report, drop, recreate, rebuild.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r"cache shm: control segment \S+ is \d+ bytes, not this build's \d+; dropping it",
            'ts2 must report the control segment size mismatch')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts2 must recreate the control segment after the drop')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: failed to create control segment',
            'the drop must leave the name free, so the O_EXCL create cannot fail with EEXIST')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'\(fast restart, recovery skipped\)', 'ts2 must rebuild from disk, never fast-attach a foreign-size segment')

        # ts3 fast-attaches what ts2 created: the drop healed, it did not just defer.
        self.ts3.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: attaching up to \d+ stripes \(fast restart', 'ts3 should attach the segment ts2 created')
        self.ts3.Disk.diags_log.Content += Testers.ContainsExpression(
            r"attaching cached directory from shm for '.+' \(fast restart", 'ts3 should reuse the per-stripe directory from shm')
        self.ts3.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: control segment \S+ is \d+ bytes', 'ts3 should see a segment of the expected size')
        self.ts3.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: creating fresh control segment', 'ts3 should not have to create another control segment')

    def _get(self, ts, description):
        tr = Test.AddTestRun(description)
        tr.Processes.Default.StartBefore(ts)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{ts.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('200', f'{description}: should return 200')
        tr.StillRunningAfter = ts

    def _clean_shutdown(self, ts, name):
        tr = Test.AddTestRun(f'Drain and clean-shutdown {name}')
        tr.Processes.Default.Env = ts.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} {name} --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _grow_control_segment(self):
        # ts1 is dead; the segment is just a file now. Extend it without touching the
        # header, so only its size makes it foreign to this build.
        tr = Test.AddTestRun('Grow the shm control segment past this build\'s size')
        tr.Processes.Default.Command = (
            f'{sys.executable} ./{self.POKE_SCRIPT} {self._control_file} {self.GROW_OFFSET} {self.GROW_BYTE_HEX}')
        tr.Processes.Default.ReturnCode = 0

    def _cleanup_shm(self):
        tr = Test.AddTestRun('Unlink the test shm segments')
        tr.Processes.Default.Env = self.ts3.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm clear --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
            'Invalid argument', 'clear must skip tombstoned slots, not fail on them')

    def run(self):
        self._get(self.ts1, 'Cold-start ts1 and cache an object')
        self._clean_shutdown(self.ts1, 'shmz_ts1')
        self._grow_control_segment()
        self._get(self.ts2, 'Start ts2; the foreign-size segment is dropped and recreated')
        self._clean_shutdown(self.ts2, 'shmz_ts2')
        self._get(self.ts3, 'Start ts3; the recreated segment fast-attaches')
        self._clean_shutdown(self.ts3, 'shmz_ts3')
        self._cleanup_shm()


CacheShmControlSizeMismatchTest().run()
