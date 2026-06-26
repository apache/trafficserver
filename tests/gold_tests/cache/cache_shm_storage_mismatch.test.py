'''
Verify that a changed storage layout never fast-attaches a stale directory.
A storage.yaml change no longer drops the whole shm control segment; instead
each stripe is matched to its prior segment by its own identity. ts1 caches an
object against one storage file and clean-shuts-down (marking the shm clean);
ts2 starts against a *different* storage file but the *same* shm name prefix.
ts2 finds ts1's control segment, keeps it (partial attach), but because its
stripe identity no longer matches any recorded entry it creates a fresh stripe
segment and reclaims ts1's orphaned one -- it must never fast-attach a segment
that describes a different on-disk layout.
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
import sys
import uuid

Test.Summary = '''
A changed storage layout never fast-attaches a stale directory: the control
segment is kept (partial attach), the relocated stripe creates a fresh segment,
and the orphaned prior segment is reclaimed.
'''
Test.ContinueOnFail = True


class CacheShmStorageMismatchTest:
    """
    The storage signature is a fingerprint of every span's path and geometry,
    stored in the shm control header. It is no longer a hard gate: a storage
    change keeps the control segment and lets each stripe attach by its own
    identity (its hash_text, which includes the disk path). This test points
    ts1 and ts2 at different storage files (a repath) while sharing one shm
    prefix, and asserts ts2:
      - keeps the existing control segment (does NOT recreate it),
      - enters partial-attach mode because the storage signature changed,
      - never fast-attaches any stripe segment (its identity differs, so the
        stale directory built for storage A is never reused),
      - creates a fresh stripe segment for its own (storage B) layout,
      - reclaims ts1's now-orphaned stripe segment,
      - and still serves a request (200).
    Because the storage change does not gate the clean-shutdown check, ts2 must
    NOT report the prior run as unclean: the only reason for the recreate is the
    storage change.
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    def __init__(self):
        self._setup_shared_state()
        # ts1 and ts2 share the shm prefix but use different storage files.
        self.ts1 = self._configure_ts('shms_ts1', self._storage_path_a)
        self.ts2 = self._configure_ts('shms_ts2', self._storage_path_b)
        self._add_diags_log_assertions()
        self._url_path = f'/cache/40/{uuid.uuid4()}'

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        # Two distinct storage files -> distinct span paths -> distinct
        # storage signatures, which is exactly the "repath" case under test.
        self._storage_path_a = os.path.join(shared_storage_dir, 'disk_a.img')
        self._storage_path_b = os.path.join(shared_storage_dir, 'disk_b.img')
        for path in (self._storage_path_a, self._storage_path_b):
            with open(path, 'ab') as f:
                f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 's' = storage-mismatch variant.
        self._shm_prefix = f'/cshms-{os.getpid() % 100000}-'

    def _configure_ts(self, name, storage_path):
        ts = Test.MakeATSProcess(name)
        ts.Disk.storage_yaml.AddLines(
            [
                'cache:',
                '  spans:',
                '    - name: disk.0',
                f'      path: {storage_path}',
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
        # ts1 cold start against storage A, clean shutdown.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'created stripe \S+ \(\d+ bytes\) for key=', 'ts1 should create at least one shm-backed stripe segment')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')

        # ts2 start against storage B: the storage signature differs, so the
        # control segment is kept (partial attach) but the relocated stripe
        # creates a fresh segment rather than fast-attaching the stale one.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'attaching up to \d+ stripes \(fast restart, partial -- storage changed\)',
            'ts2 must enter partial-attach mode after the storage change')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'created stripe \S+ \(\d+ bytes\) for key=', 'ts2 must create a fresh stripe segment for its own layout')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: reclaiming orphaned stripe segment', "ts2 must reclaim ts1's orphaned stripe segment")
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'reclaimed \d+ orphaned stripe segment\(s\) after storage change', 'ts2 must report the reclaim summary')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'attached stripe \S+ \(\d+ bytes\) for key=',
            'ts2 must never fast-attach a stripe segment built for a different layout')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: creating fresh control segment', 'ts2 must keep the control segment across the storage change')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: (schema|ABI) mismatch', 'the recreate must be due to the storage change, not schema/ABI')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly',
            'the recreate must be due to the storage change, not an unclean shutdown')

    def _populate_cache(self):
        tr = Test.AddTestRun('Populate cache via ts1 (storage A)')
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
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shms_ts1 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _verify_partial_attach_and_reclaim(self):
        tr = Test.AddTestRun('Start ts2 (storage B); verify partial attach: fresh stripe + orphan reclaim')
        tr.Processes.Default.StartBefore(self.ts2)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts2.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            '200', 'ts2 should serve correctly after the partial attach')
        tr.StillRunningAfter = self.ts2

    def _clean_shutdown_ts2(self):
        # Stop ts2 before clearing the shm: `cache shm clear` refuses to unlink a
        # segment a live traffic_server still owns, so the segments must be
        # ownerless (clean_shutdown clears owner_pid) before cleanup runs.
        tr = Test.AddTestRun('Drain and clean-shutdown ts2')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shms_ts2 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _cleanup_shm(self):
        # A clean shutdown deliberately keeps the control + live stripe segments
        # for the next fast restart, so they outlive the test. Unlink them by
        # prefix to avoid leaking POSIX shm across repeated local runs (macOS has
        # no /dev/shm to clear out of band).
        tr = Test.AddTestRun('Unlink the test shm segments')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm clear --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
            'Invalid argument', 'clear must skip tombstoned slots, not fail on them')

    def run(self):
        self._populate_cache()
        self._clean_shutdown_ts1()
        self._verify_partial_attach_and_reclaim()
        self._clean_shutdown_ts2()
        self._cleanup_shm()


CacheShmStorageMismatchTest().run()
