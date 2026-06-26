'''
Verify per-stripe partial attach when a disk is dropped from storage.yaml
(the "bad disk" case). A storage change no longer cold-starts every stripe:
the stripes on healthy, unchanged disks fast-attach their prior shm segments
while the segment left behind by the removed disk is reclaimed.

ts1 caches an object across two disks and clean-shuts-down (marking the shm
clean). ts2 starts against the *same* shm prefix but with the second disk
removed from storage.yaml -- simulating a bad disk dropped by the operator.
ts2 must:
  - keep the existing control segment (partial attach, not a full recreate),
  - fast-attach the surviving disk's stripe by its stable identity,
  - reclaim the orphaned stripe segment of the removed disk,
  - and still serve traffic.
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
Dropping a disk from storage.yaml fast-attaches the surviving stripes from
shm and reclaims the orphaned stripe segment of the removed disk.
'''
Test.ContinueOnFail = True


class CacheShmBadDiskDroppedTest:
    """
    A stripe's shm identity is its hash_text -- the disk seed (path or
    hash_base_string) plus that disk's own dir_skip:blocks, read from the
    disk's persisted header. None of those depend on the other disks, so when
    one disk is removed from storage.yaml the surviving disks compute the
    same hash_text as before and re-attach their prior shm segments. The
    removed disk's stripe is no longer present, so its control entry is never
    claimed and finalize_attach() reclaims the orphaned segment.

    ts1 starts cold across disk_a + disk_b, populates the cache, and clean-shuts
    down. ts2 starts against disk_a only (disk_b "fails"/dropped) sharing the
    shm prefix, and asserts ts2:
      - enters partial-attach mode (storage signature changed) keeping the
        control segment rather than recreating it,
      - fast-attaches the surviving disk_a stripe from shm,
      - reclaims exactly the orphaned disk_b stripe segment,
      - reports neither an unclean shutdown nor a schema/ABI mismatch,
      - and serves a request (200).
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB per disk

    def __init__(self):
        self._setup_shared_state()
        # ts1 sees both disks; ts2 sees only disk_a (disk_b dropped).
        self.ts1 = self._configure_ts('shmbd_ts1', [self._storage_path_a, self._storage_path_b])
        self.ts2 = self._configure_ts('shmbd_ts2', [self._storage_path_a])
        self._add_diags_log_assertions()
        self._url_path = f'/cache/40/{uuid.uuid4()}'

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        # Absolute paths keep the spans independent of MakeATSProcess's
        # per-instance STORAGEDIR so disk_a has identical geometry for ts1 and
        # ts2 (hence identical stripe identity -> fast attach).
        self._storage_path_a = os.path.join(shared_storage_dir, 'disk_a.img')
        self._storage_path_b = os.path.join(shared_storage_dir, 'disk_b.img')
        for path in (self._storage_path_a, self._storage_path_b):
            with open(path, 'ab') as f:
                f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'bd' = bad-disk-dropped variant.
        self._shm_prefix = f'/cshmbd-{os.getpid() % 100000}-'

    def _configure_ts(self, name, storage_paths):
        ts = Test.MakeATSProcess(name)
        storage_lines = ['cache:', '  spans:']
        for i, storage_path in enumerate(storage_paths):
            storage_lines += [
                f'    - name: disk.{i}',
                f'      path: {storage_path}',
                f'      size: {self.SHARED_DISK_SIZE_BYTES}',
            ]
        storage_lines += ['  volumes:', '    - id: 1', '      scheme: http', '      size: 100%']
        ts.Disk.storage_yaml.AddLines(storage_lines)
        ts.Disk.records_config.update(
            {
                'proxy.config.cache.shm.enabled': 1,
                'proxy.config.cache.shm.name_prefix': self._shm_prefix,
                'proxy.config.cache.shm.use_hugepages': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cache_shm|cache_init',
                'proxy.config.diags.output.diag': 'L',
                'proxy.config.http.wait_for_cache': 1,
            })
        ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,via')
        ts.Disk.remap_config.AddLine('map / http://127.0.0.1/ @plugin=generator.so')
        return ts

    def _add_diags_log_assertions(self):
        # ts1 cold start across both disks, clean shutdown.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'created stripe \S+ \(\d+ bytes\) for key=', 'ts1 should create the shm-backed stripe segments')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')

        # ts2 warm start with disk_b dropped: partial attach -- the surviving
        # disk_a stripe attaches, the orphaned disk_b segment is reclaimed.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'attaching up to \d+ stripes \(fast restart, partial -- storage changed\)',
            'ts2 must enter partial-attach mode after the disk was dropped')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'attached stripe \S+ \(\d+ bytes\) for key=', 'ts2 must fast-attach the surviving disk_a stripe from shm')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: reclaiming orphaned stripe segment', 'ts2 must reclaim the dropped disk_b stripe segment')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'reclaimed \d+ orphaned stripe segment\(s\) after storage change', 'ts2 must report the reclaim summary')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: creating fresh control segment', 'ts2 must keep the control segment across the disk drop')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly',
            'the partial attach must be due to the disk drop, not an unclean shutdown')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: (schema|ABI) mismatch', 'the partial attach must be due to the disk drop, not schema/ABI')

    def _populate_cache(self):
        tr = Test.AddTestRun('Populate cache via ts1 (disk_a + disk_b)')
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
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shmbd_ts1 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _dump_shm_state(self):
        # Between ts1's clean shutdown and ts2's start the control segment is
        # marked clean and records both stripes (nothing reclaimed yet). Capture
        # it with `traffic_ctl cache shm status` and compare against a gold file.
        # The gold masks the run-specific names, the ABI/storage hashes, and the
        # page-rounded sizes with the `` wildcard, so what is asserted literally
        # is the meaningful state: valid magic/schema, clean_shutdown=1,
        # stripe_count=2, and both stripe segments present.
        tr = Test.AddTestRun('Dump shm control state after ts1 clean shutdown')
        # Use ts1's Env: it has been started, so the per-instance bin dir is on
        # PATH (ts2's Env only gains it once ts2 starts, which is the next step).
        # `cache shm status` reads the segment directly and needs no live server.
        tr.Processes.Default.Env = self.ts1.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm status --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = 'gold/cache_shm_state_after_shutdown.gold'

    def _verify_survivor_attach_and_reclaim(self):
        tr = Test.AddTestRun('Start ts2 (disk_b dropped); verify survivor fast-attach + orphan reclaim')
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
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shmbd_ts2 --signal TERM && sleep 3')
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
        self._dump_shm_state()
        self._verify_survivor_attach_and_reclaim()
        self._clean_shutdown_ts2()
        self._cleanup_shm()


CacheShmBadDiskDroppedTest().run()
