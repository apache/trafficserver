'''
Verify the concurrent-attach guard: a second traffic_server must never map the
shm directory read-write underneath a live owner. ts1 cold-starts and becomes
the owner of the control segment (it sets owner_pid and, on Linux, holds an
exclusive flock for its lifetime). While ts1 is still running, ts2 starts
against the *same* shm prefix; it must refuse shm for this run, disable it, and
come up on its own disk cache without touching ts1's segment. ts1 keeps serving
throughout.

The two instances use *separate* on-disk cache files so the test isolates the
shm concurrent-attach guard from any contention over a shared cache file.
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
A second traffic_server refuses to attach the shm directory while a live owner
holds it, disabling shm for its run instead of attaching concurrently.
'''
Test.ContinueOnFail = True


class CacheShmConcurrentAttachTest:
    """
    The concurrent-attach guard (P0). A live owner is still mapping the Dir
    read-write; a second writer would corrupt it, and clean_shutdown is no
    protection against a concurrent *live* run. The guard fires from either of
    two mechanisms, so this test asserts on the shared tail of both messages:
      - Linux: ts1 holds an exclusive flock on the control segment for its
        lifetime; ts2's lock attempt returns HeldByOther ("... is locked by a
        live owner ...").
      - macOS (flock unsupported): the owner_pid liveness backstop fires
        instead ("... claims a live owner ...").
    Both end in "disabling shm this run to avoid concurrent attach" and set the
    run to shm-disabled, which is what this test pins -- so it runs on every
    platform.

    ts2 must:
      - log the concurrent-attach refusal,
      - NOT create or attach a control segment (it bails before either),
      - still serve a request (200) from its own disk cache.
    ts1 must keep running and serving the whole time.
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    def __init__(self):
        self._setup_shared_state()
        # Same shm prefix, different storage files: the collision under test is
        # purely on the shm control segment, not the on-disk cache.
        self.ts1 = self._configure_ts('shmc_ts1', self._storage_path_a)
        self.ts2 = self._configure_ts('shmc_ts2', self._storage_path_b)
        self._add_diags_log_assertions()
        self._url_path = f'/cache/40/{uuid.uuid4()}'

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        self._storage_path_a = os.path.join(shared_storage_dir, 'disk_a.img')
        self._storage_path_b = os.path.join(shared_storage_dir, 'disk_b.img')
        for path in (self._storage_path_a, self._storage_path_b):
            with open(path, 'ab') as f:
                f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'c' = concurrent-attach variant.
        self._shm_prefix = f'/cshmc-{os.getpid() % 100000}-'

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
        # ts1 is the owner: it creates the fresh control segment.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create and own the shm control segment')

        # ts2 starts while ts1 owns the segment: it must refuse and disable shm.
        # The message head differs by platform (flock vs owner_pid backstop); the
        # tail is common, so anchor on it.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'disabling shm this run to avoid concurrent attach', 'ts2 must refuse to attach while ts1 owns the segment')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: creating fresh control segment', 'ts2 must not create a control segment when it refuses shm')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: attaching up to \d+ stripes \(fast restart', "ts2 must not attach ts1's live control segment")

    def _start_owner(self):
        tr = Test.AddTestRun('Cold-start ts1 (becomes the shm owner)')
        tr.Processes.Default.StartBefore(self.ts1)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts1.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('200', 'ts1 GET should return 200')
        tr.StillRunningAfter = self.ts1

    def _start_second_refused(self):
        # ts1 is still running (kept alive by StillRunningAfter above), so ts2's
        # start hits the concurrent-attach guard.
        tr = Test.AddTestRun('Start ts2 while ts1 is live; ts2 must refuse shm and serve from disk')
        tr.Processes.Default.StartBefore(self.ts2)
        tr.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}\\n" '
            f'-H "x-debug: x-cache,via" '
            f'http://127.0.0.1:{self.ts2.Variables.port}{self._url_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            '200', 'ts2 should serve from its own disk cache with shm disabled')
        tr.StillRunningAfter = self.ts1
        tr.StillRunningAfter = self.ts2

    def _clean_shutdown(self, ts, name):
        tr = Test.AddTestRun(f'Drain and clean-shutdown {name}')
        tr.Processes.Default.Env = ts.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} {name} --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0

    def _cleanup_shm(self):
        # ts1 (the owner) is stopped before this so clean_shutdown clears
        # owner_pid; otherwise `cache shm clear` refuses a live owner.
        tr = Test.AddTestRun('Unlink the test shm segments')
        tr.Processes.Default.Env = self.ts1.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm clear --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
            'Invalid argument', 'clear must skip tombstoned slots, not fail on them')

    def run(self):
        self._start_owner()
        self._start_second_refused()
        # Stop the non-owner first, then the owner (clears owner_pid), then clear.
        self._clean_shutdown(self.ts2, 'shmc_ts2')
        self._clean_shutdown(self.ts1, 'shmc_ts1')
        self._cleanup_shm()


CacheShmConcurrentAttachTest().run()
