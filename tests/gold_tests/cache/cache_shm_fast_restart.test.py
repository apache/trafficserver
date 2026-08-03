'''
Verify the cache directory survives a clean shutdown via shared memory and is
attached on the next start (fast restart). Two ATS instances share an on-disk
cache file and a POSIX shm name prefix; ts1 populates the cache and is shut
down via traffic_ctl drain + SIGTERM, then ts2 starts and serves the same URL
out of cache without re-fetching from the origin.

Traffic is driven with Proxy Verifier: a single verifier-server acts as the
origin and the verifier-client replays cache-shm-fast-restart.replay.yaml --
the "fill" transaction against ts1 and the "hit" transaction against ts2.
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

Test.Summary = '''
Cache directory survives clean shutdown via POSIX shared memory.
'''
Test.ContinueOnFail = True


class CacheShmFastRestartTest:
    """
    Cover the cache shm fast-restart scenario end-to-end.

    Sequence:
      1. ts1 cold-start: creates a fresh shm control segment and per-stripe
         segments; populates the cache via the "fill" transaction (cache miss,
         fetched from the verifier-server origin).
      2. ts1 is drained and SIGTERM'd. The shutdown hook flushes the directory
         and marks the shm clean.
      3. ts2 starts against the same on-disk file and shm prefix: attaches the
         existing control segment, attaches per-stripe segments, and reuses the
         cached directory without re-reading it from disk.
      4. ts2 serves the same URL out of cache via the "hit" transaction
         (X-Cache: hit-fresh). The transaction's origin response is a 502
         sentinel, so any forward to the origin would fail the run.

    Each step is verified both at the response level (proxy-verifier) and via
    diags-log assertions on the cache_shm / cache_dir_init code paths.
    """

    # Helper script for sending signals to a traffic_server process by command-line
    # identifier match. Reused from gold_tests/logging.
    TS_PID_SCRIPT = 'ts_process_handler.py'

    # The replay file driving both the populate ("fill") and verify ("hit")
    # transactions. They share a cache key and differ only by uuid.
    REPLAY_FILE = 'replay/cache-shm-fast-restart.replay.yaml'

    # Stripe size for the shared cache. Must be large enough that the directory
    # contains real entries; small enough that the disk.img is cheap to create.
    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    def __init__(self):
        self._setup_shared_state()
        # A single verifier-server is the origin for both ts1 and ts2. It is
        # started before ts1 and kept running across the whole test.
        self.server = Test.MakeVerifierServerProcess('shm-origin', self.REPLAY_FILE)
        self.ts1 = self._configure_ts('shm_ts1')
        self.ts2 = self._configure_ts('shm_ts2')
        self._add_diags_log_assertions()

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))

        # Shared storage file used by both ts1 and ts2. The absolute path makes
        # storage.yaml independent of MakeATSProcess's per-instance STORAGEDIR.
        # ATS opens regular-file spans with O_RDONLY first to stat them -- it
        # does not auto-create the backing file -- so pre-create disk.img at the
        # configured size before either ts starts.
        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        self._shared_storage_path = os.path.join(shared_storage_dir, 'disk.img')
        with open(self._shared_storage_path, 'ab') as f:
            f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # POSIX shm names: macOS PSHMNAMLEN limit is 31 chars including '/'.
        # Keep the prefix short and unique per test run so concurrent autest
        # runs do not collide.
        self._shm_prefix = f'/cshm-{os.getpid() % 100000}-'

    def _configure_ts(self, name):
        ts = Test.MakeATSProcess(name)
        # Master configures cache storage via storage.yaml. An absolute span path
        # keeps the span independent of MakeATSProcess's per-instance STORAGEDIR so
        # ts1 and ts2 share the same on-disk cache, which yields identical stripe
        # geometry (hence identical shm identity).
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
                # The per-stripe 'created/attached stripe' lines are Dbg() calls;
                # route debug output to diags.log (default is stderr) so the
                # ContainsExpression assertions below can match them.
                'proxy.config.diags.output.diag': 'L',
                'proxy.config.http.wait_for_cache': 1,
            })
        ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,via')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.http_port}/')
        return ts

    def _add_diags_log_assertions(self):
        # These assertions match the *stable core* of each cache-shm log line and
        # deliberately stop before the trailing parenthetical qualifier. The shm
        # code appends optional context to several of these messages as it evolves
        # -- "attaching N stripes" became "attaching up to N stripes",
        # "(fast restart)" became "(fast restart, recovery skipped)" on the stripe
        # path and "(fast restart, partial -- storage changed)" on the control
        # path. Anchoring on the invariant prefix (not the closing paren) keeps the
        # test from breaking every time such a qualifier is added. Likewise, the
        # excludes name only log strings that actually exist in the source: an
        # exclude on a non-existent string can never fire and gives false comfort.

        # ts1 (cold start): creates fresh shm, marks it clean on shutdown, and must
        # NOT report any "drop" reason since there is nothing to drop.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: created stripe \S+ \(\d+ bytes\) for key=', 'ts1 should create at least one shm-backed stripe segment')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')
        self.ts1.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: (schema|ABI) mismatch', 'ts1 should not detect any shm mismatch on cold start')
        self.ts1.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly', 'ts1 should not see a dirty shm on cold start')
        self.ts1.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: stripe \S+ size mismatch', 'ts1 should not see a stripe size mismatch on cold start')

        # ts2 (warm start): attaches the existing control segment, fast-attaches the
        # per-stripe segment, reuses the cached directory, and must NOT fall back to
        # the disk-rebuild path.
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: attaching up to \d+ stripes \(fast restart', 'ts2 should attach the existing shm (fast restart)')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: attached stripe \S+ \(\d+ bytes\) for key=', 'ts2 should attach at least one shm-backed stripe segment')
        self.ts2.Disk.diags_log.Content += Testers.ContainsExpression(
            r"attaching cached directory from shm for '.+' \(fast restart", 'ts2 should reuse the per-stripe directory from shm')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: creating fresh control segment', 'ts2 should not create a fresh control segment')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: (schema|ABI) mismatch', 'ts2 should not detect any shm mismatch on warm start')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly', 'ts2 should see the shm marked clean')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'shm directory invalid for', 'ts2 should not fall back from shm to disk read')
        self.ts2.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: stripe \S+ size mismatch', 'ts2 should fast-attach without a stripe size-mismatch recreate')

    def _start_ts1(self):
        # Cold start ts1 against the verifier-server origin and replay the
        # "fill" transaction: a cache miss that ATS fetches and stores.
        tr = Test.AddTestRun('Start ts1, then cache contents (fill)')
        tr.AddVerifierClientProcess(
            'shm-fill-client', self.REPLAY_FILE, http_ports=[self.ts1.Variables.port], keys='fill', other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts1)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts1

    def _clean_shutdown_ts1(self):
        # Drain + SIGTERM ts1. SIGTERM goes through AutoStopCont which invokes
        # TS_LIFECYCLE_SHUTDOWN_HOOK -> sync_cache_dir_on_shutdown ->
        # CacheShm::mark_clean_shutdown.
        tr = Test.AddTestRun('Drain and clean-shutdown ts1')
        tr.Processes.Default.Env = self.ts1.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shm_ts1 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server

    def _start_ts2(self):
        # ts2 attaches the CacheDir from the shm created by ts1. Replay the
        # "hit" transaction: ATS must serve it from cache (X-Cache: hit-fresh)
        # without contacting the origin -- the replay's 502 sentinel response
        # would otherwise surface as a proxy-response mismatch.
        tr = Test.AddTestRun('Start ts2; verify shm fast-attach and cache HIT')
        tr.AddVerifierClientProcess(
            'shm-hit-client', self.REPLAY_FILE, http_ports=[self.ts2.Variables.port], keys='hit', other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.ts2)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts2

    def _clean_shutdown_ts2(self):
        # Stop ts2 before clearing the shm: `cache shm clear` refuses to unlink a
        # segment a live traffic_server still owns, so the segments must be
        # ownerless (clean_shutdown clears owner_pid) before cleanup runs.
        tr = Test.AddTestRun('Drain and clean-shutdown ts2')
        tr.Processes.Default.Env = self.ts2.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} shm_ts2 --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server

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
        self._start_ts1()
        self._clean_shutdown_ts1()
        self._start_ts2()
        self._clean_shutdown_ts2()
        self._cleanup_shm()


CacheShmFastRestartTest().run()
