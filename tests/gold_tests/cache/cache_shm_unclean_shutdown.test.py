'''
Verify the shm fast-restart path refuses to trust a directory left by a crash.
A clean shutdown marks the control segment clean; a crash (SIGKILL) does not,
so clean_shutdown stays 0. ts1 cold-starts, caches an object, and is *killed*
(no drain, no SIGTERM) so the shutdown hook never runs. ts2 starts against the
same on-disk cache and shm prefix: it must find the dirty segment, drop the
whole thing, rebuild the directory from disk, and never take the fast-attach
"recovery skipped" path.
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
An unclean shutdown (SIGKILL) leaves the shm control segment dirty, so the next
start drops it and rebuilds the directory from disk instead of fast-attaching.
'''
Test.ContinueOnFail = True


class CacheShmUncleanShutdownTest:
    """
    The crash-safety gate. clean_shutdown is set to 1 only by the shutdown hook
    (CacheShm::mark_clean_shutdown); a SIGKILL bypasses it, leaving the segment
    with clean_shutdown == 0. On the next start the control segment is found but
    rejected -- a crash may have left dir entries pointing at content that never
    reached disk, so no stripe can safely skip recovery. ts2 must:
      - log "previous run did not shutdown cleanly, dropping",
      - recreate a fresh control segment,
      - NOT take the stripe fast-attach "recovery skipped" path,
      - and still serve a request (200).

    This gate is cross-platform: clean_shutdown lives in the control segment, so
    it does not depend on the Linux-only flock path.
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    def __init__(self):
        self._setup_shared_state()
        # ts1 and ts2 share the same on-disk cache file and shm prefix so ts2
        # would fast-attach ts1's directory -- were it not left dirty by the kill.
        self.ts1 = self._configure_ts('shmu_ts1')
        # ts1 is SIGKILLed mid-test, so it exits on signal 9 (returncode -9, or
        # 137 where the runner reports 128+signal). Declare that expected exit so
        # the managed-process check does not flag the deliberate kill. ts1 still
        # starts normally, so leave Ready at its default (port-open) condition.
        self.ts1.ReturnCode = Any(-9, 137)
        self.ts2 = self._configure_ts('shmu_ts2')
        self._add_diags_log_assertions()
        self._url_path = f'/cache/40/{uuid.uuid4()}'

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        self._shared_storage_path = os.path.join(shared_storage_dir, 'disk.img')
        with open(self._shared_storage_path, 'ab') as f:
            f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'u' = unclean-shutdown variant.
        self._shm_prefix = f'/cshmu-{os.getpid() % 100000}-'

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
        # ts1 cold start: creates a fresh segment but is killed before it can mark
        # the shutdown clean.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: marking clean shutdown', 'ts1 is SIGKILLed, so it must never mark the shm clean')

        # ts2 start: finds the dirty segment, drops it, recreates, and rebuilds
        # from disk -- it must NOT fast-attach.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: previous run did not shutdown cleanly, dropping', 'ts2 must reject the dirty segment left by the crash')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts2 must recreate the control segment after dropping the dirty one')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'\(fast restart, recovery skipped\)', 'ts2 must rebuild from disk, never take the fast-attach path')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: attaching up to \d+ stripes \(fast restart', 'ts2 must not attach the dirty control segment')

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

    def _kill_ts1(self):
        # SIGKILL -- no drain, no SIGTERM -- so the shutdown hook never runs and
        # the control segment is left with clean_shutdown == 0.
        tr = Test.AddTestRun('SIGKILL ts1 (unclean shutdown)')
        tr.Processes.Default.Env = self.ts1.Env
        tr.Processes.Default.Command = (f'{sys.executable} ./{self.TS_PID_SCRIPT} shmu_ts1 --signal KILL && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _verify_dirty_drop(self):
        tr = Test.AddTestRun('Start ts2; verify the dirty segment is dropped and rebuilt from disk')
        tr.Processes.Default.StartBefore(self.ts2)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts2.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            '200', 'ts2 should serve correctly after dropping the dirty segment')
        tr.StillRunningAfter = self.ts2

    def _clean_shutdown_ts2(self):
        tr = Test.AddTestRun('Drain and clean-shutdown ts2')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shmu_ts2 --signal TERM && sleep 3')
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
        self._kill_ts1()
        self._verify_dirty_drop()
        self._clean_shutdown_ts2()
        self._cleanup_shm()


CacheShmUncleanShutdownTest().run()
