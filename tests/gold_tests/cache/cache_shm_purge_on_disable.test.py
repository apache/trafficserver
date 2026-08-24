'''
Purge-stale-on-start: when shm is disabled but a prior run left segments behind,
proxy.config.cache.shm.purge_stale_on_start=1 removes them at startup.

This guards two hazards of running with the feature disabled after it had been
enabled (see records.yaml docs): (a) the leftover segments keep consuming tmpfs
the disabled instance never reads, and (b) a later re-enabled run would otherwise
fast-attach a directory that went stale while ATS ran disabled (writing only to
disk).

Three scenarios, each on its own shm prefix + on-disk storage so they do not
interact:

  - PURGE (positive): a seed instance runs shm-enabled and clean-shuts-down,
    leaving a clean control + stripe segment. A second instance runs disabled
    with purge_stale_on_start=1 and must remove them. Confirmed three ways: the
    seed's "clean" segment exists before (traffic_ctl cache shm status, exit 0),
    the disabled instance logs the purge Note, and the segment is gone after
    (status exits 2, "not found").

  - KEEP (negative): same seed, but the disabled instance has
    purge_stale_on_start=0. It must NOT log the purge and the segment must remain.

  - NOOP (no leftover): a disabled instance with purge_stale_on_start=1 against a
    never-used prefix must do nothing quietly -- no purge Note, no "cannot open"
    warning.

The segments are inspected with traffic_ctl (POSIX shm is not path-addressable on
macOS, so /dev/shm cannot be listed directly).
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
import re
import sys

Test.Summary = '''
shm.purge_stale_on_start removes leftover shm segments at startup when shm is
disabled, only when set, and only when a <prefix>control segment exists.
'''
Test.ContinueOnFail = True


class CacheShmPurgeOnDisableTest:

    TS_PID_SCRIPT = 'ts_process_handler.py'
    DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB; matches the other shm gold tests.

    # CtrlCommand sets this exit code when a shm control segment is absent/invalid
    # (src/traffic_ctl/TrafficCtlStatus.h).
    CTRL_EX_ERROR = 2

    PURGE_NOTE = r"cache shm: purged stale segments while disabled \(removed [1-9]"

    def __init__(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        pid = os.getpid() % 100000
        # Each control name is "<prefix>control"; keep well under macOS PSHMNAMLEN (31).
        self._prefix_purge = f'/cshmp-{pid}-'  # positive: must be purged
        self._prefix_keep = f'/cshmk-{pid}-'  # negative: must remain
        self._prefix_noop = f'/cshmz-{pid}-'  # no leftover: nothing to do

        # Seed (shm enabled) instances that create the leftover segments.
        self.seed_purge = self._make_ts('cshm_seed_p', self._prefix_purge, 'disk_p.img', enabled=True, purge=False)
        self.seed_keep = self._make_ts('cshm_seed_k', self._prefix_keep, 'disk_k.img', enabled=True, purge=False)

        # Disabled instances under test.
        self.run_purge = self._make_ts('cshm_run_p', self._prefix_purge, 'disk_p.img', enabled=False, purge=True)
        self.run_keep = self._make_ts('cshm_run_k', self._prefix_keep, 'disk_k.img', enabled=False, purge=False)
        self.run_noop = self._make_ts('cshm_run_z', self._prefix_noop, 'disk_z.img', enabled=False, purge=True)

        self._add_diags_assertions()

    def _make_ts(self, name, prefix, disk_name, enabled, purge):
        disk_path = self._ensure_disk(disk_name)
        ts = Test.MakeATSProcess(name)
        ts.Disk.storage_yaml.AddLines(
            [
                'cache:',
                '  spans:',
                '    - name: disk.0',
                f'      path: {disk_path}',
                f'      size: {self.DISK_SIZE_BYTES}',
                '  volumes:',
                '    - id: 1',
                '      scheme: http',
                '      size: 100%',
            ])
        ts.Disk.remap_config.AddLine('map / http://127.0.0.1:8080/')  # never exercised; keeps remap.config non-empty
        ts.Disk.records_config.update(
            {
                'proxy.config.cache.shm.enabled': 1 if enabled else 0,
                'proxy.config.cache.shm.name_prefix': prefix,
                'proxy.config.cache.shm.use_hugepages': 0,
                'proxy.config.cache.shm.purge_stale_on_start': 1 if purge else 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cache_shm',
                'proxy.config.diags.output.diag': 'L',
                'proxy.config.http.wait_for_cache': 1,
            })
        return ts

    def _ensure_disk(self, disk_name):
        storage_dir = os.path.join(Test.RunDirectory, 'storage')
        os.makedirs(storage_dir, exist_ok=True)
        path = os.path.join(storage_dir, disk_name)
        if not os.path.exists(path):
            with open(path, 'ab') as f:
                f.truncate(self.DISK_SIZE_BYTES)
        return path

    def _add_diags_assertions(self):
        # Seeds create a fresh control segment and mark it clean on the way out --
        # that is the "fast-attachable but now stale" state the purge must clean up.
        for seed in (self.seed_purge, self.seed_keep):
            seed.Disk.diags_log.Content += Testers.ContainsExpression(
                r'cache shm: creating fresh control segment', 'seed should create a fresh shm control segment')
            seed.Disk.diags_log.Content += Testers.ContainsExpression(
                r'cache shm: marking clean shutdown', 'seed should mark the shm clean before exit')

        # Positive: the disabled+purge instance logs the purge of at least one segment.
        self.run_purge.Disk.diags_log.Content += Testers.ContainsExpression(
            self.PURGE_NOTE, 'disabled instance with purge_stale_on_start=1 should purge the leftover segments')

        # Negative: purge_stale_on_start=0 must never purge.
        self.run_keep.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: purged stale segments', 'purge_stale_on_start=0 must not purge')

        # No-op: nothing exists for this prefix, so neither a purge nor an error.
        self.run_noop.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: purged stale segments', 'no leftover means nothing is purged')
        self.run_noop.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: cannot open control segment', 'a missing control segment is a quiet no-op, not a warning')

    def _shm_status(self, description, ts, prefix, expect_present):
        """Run `traffic_ctl cache shm status` and assert the control segment is (not) there."""
        control_name = prefix + 'control'
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Env = ts.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm status --prefix {prefix}'
        if expect_present:
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
                r'Control segment:\s+' + re.escape(control_name), 'control segment should be present')
        else:
            tr.Processes.Default.ReturnCode = self.CTRL_EX_ERROR
            tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
                r"control segment '" + re.escape(control_name) + r"' not found", 'control segment should be gone')
        return tr

    def _start_seed(self, description, seed, prefix):
        # Starting the seed (shm enabled) creates the control + stripe segments; the
        # status probe also confirms they exist while the seed is the live owner.
        tr = self._shm_status(description, seed, prefix, expect_present=True)
        tr.Processes.Default.StartBefore(seed)
        tr.StillRunningAfter = seed
        return tr

    def _clean_shutdown(self, description, seed, name):
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Env = seed.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} {name} --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _start_disabled(self, description, ts, prefix, expect_present):
        # Start the disabled instance under test; its purge (or no-op) runs during
        # cache init, so by the time it is ready the status below reflects the result.
        tr = self._shm_status(description, ts, prefix, expect_present=expect_present)
        tr.Processes.Default.StartBefore(ts)
        tr.StillRunningAfter = ts
        return tr

    def _cleanup(self):
        tr = Test.AddTestRun('Unlink any remaining test shm segments')
        tr.Processes.Default.Env = self.run_keep.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl cache shm clear --prefix {self._prefix_purge} ; '
            f'traffic_ctl cache shm clear --prefix {self._prefix_keep} ; '
            f'traffic_ctl cache shm clear --prefix {self._prefix_noop}')
        tr.Processes.Default.ReturnCode = 0

    def run(self):
        # PURGE (positive)
        self._start_seed('PURGE: start shm-enabled seed; control segment is created', self.seed_purge, self._prefix_purge)
        self._clean_shutdown('PURGE: clean-shutdown seed (leaves a clean segment)', self.seed_purge, 'cshm_seed_p')
        # Probe with a Env whose bin/ autest has already populated (seed_purge was
        # started above); run_purge has not started yet, so its bin/ does not exist.
        self._shm_status(
            'PURGE: precondition -- clean leftover segment is present', self.seed_purge, self._prefix_purge,
            expect_present=True).Processes.Default.Streams.stdout += Testers.ContainsExpression(
                r'clean_shutdown:\s+1 \(clean\)', 'leftover segment should be marked clean (the stale-but-attachable case)')
        self._start_disabled(
            'PURGE: start disabled+purge=1; leftover segments are removed',
            self.run_purge,
            self._prefix_purge,
            expect_present=False)

        # KEEP (negative)
        self._start_seed('KEEP: start shm-enabled seed; control segment is created', self.seed_keep, self._prefix_keep)
        self._clean_shutdown('KEEP: clean-shutdown seed (leaves a clean segment)', self.seed_keep, 'cshm_seed_k')
        self._start_disabled(
            'KEEP: start disabled+purge=0; leftover segments remain', self.run_keep, self._prefix_keep, expect_present=True)

        # NOOP (no leftover)
        tr = Test.AddTestRun('NOOP: start disabled+purge=1 against an unused prefix; nothing to do')
        tr.Processes.Default.Env = self.run_noop.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm status --prefix {self._prefix_noop}'
        tr.Processes.Default.ReturnCode = self.CTRL_EX_ERROR  # never existed
        tr.Processes.Default.StartBefore(self.run_noop)
        tr.StillRunningAfter = self.run_noop

        self._cleanup()


CacheShmPurgeOnDisableTest().run()
