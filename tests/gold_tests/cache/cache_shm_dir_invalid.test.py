'''
Verify the per-stripe shm directory trust gates: an in-shm directory whose header
fields are out of range is rejected and rebuilt from disk, never fast-attached.
These branches (Stripe::_shm_directory_is_valid) are what stand between a stale or
torn shm directory and out-of-bounds disk I/O, so each is driven directly.

ts1 cold-starts, caches an object, and clean-shuts-down, which leaves the control
segment marked clean and the stripe segment holding a valid directory. The stripe
segment file under /dev/shm is then tampered with between runs:

  * ts2 sees write_pos pushed past the end of the stripe.
  * ts3 sees freelist[0] pushed past the segment's entry count.

Each instance must attach the shm segments, reject the directory, fall back to the
disk read + recover_data(), and still serve the object out of cache.

Linux-only: it pokes raw bytes in the /dev/shm segment files, which exist only on
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

Test.Summary = '''
An in-shm stripe directory with out-of-range header fields is rejected and rebuilt
from disk, never fast-attached.
'''
Test.ContinueOnFail = True

# The byte-poke drives the gate by editing /dev/shm directly, which is a Linux
# facility; macOS POSIX shm is not exposed as a file. There is no Condition for
# the platform, so gate with a lambda (ports.py branches on platform the same way).
Test.SkipUnless(Condition(lambda: platform.system() == 'Linux', "shm byte-poke gates need Linux /dev/shm"))


class CacheShmDirInvalidTest:
    """
    The per-stripe directory gates. On a fast restart the in-shm directory is used
    verbatim -- the disk read and recover_data() are both skipped -- so every header
    field the cache later trusts is range-checked first. A rejection must be safe and
    silent to clients: the stripe falls back to the disk read, recovers, and serves
    the same object.

    Sequence, per tampered field:
      - poke the field in the /dev/shm stripe segment left by a clean shutdown,
      - start the next ts, which must log "shm directory invalid ... falling back to
        disk read" and must NOT log the fast-attach line,
      - replay the cache-hit transaction, which must be served from cache.
    """

    TS_PID_SCRIPT = 'ts_process_handler.py'
    POKE_SCRIPT = 'shm_poke.py'

    REPLAY_FILE = 'replay/cache-shm-dir-invalid.replay.yaml'

    SHARED_DISK_SIZE_BYTES = 256 * 1024 * 1024  # 256 MiB

    # StripeHeaderFooter layout (P_CacheDir.h) at the head of the stripe segment:
    # magic @0, version @4, create_time @8, write_pos @16, last_write_pos @24,
    # agg_pos @32, ... sector_size @64, unused @68, freelist[0] @72.
    WRITE_POS_OFFSET = 16
    FREELIST_0_OFFSET = 72

    # Little-endian off_t 0x0000FFFFFFFFFFFF: far beyond skip + len for a 256 MiB
    # stripe, so the write_pos range check rejects it. Leaving the top two bytes
    # zero keeps the value positive, which exercises the upper-bound branch rather
    # than the negative-offset one.
    BOGUS_WRITE_POS_LE_HEX = 'ffffffffffff0000'
    # Little-endian uint16 65535. The largest value a Dir next/prev field can hold,
    # and past the entry count of any segment with fewer than 16384 buckets -- which
    # a 256 MiB stripe is.
    BOGUS_FREELIST_LE_HEX = 'ffff'

    def __init__(self):
        self._setup_shared_state()
        # A single verifier-server is the origin for every ts, started before ts1
        # and kept running for the whole test.
        self.server = Test.MakeVerifierServerProcess('shmd-origin', self.REPLAY_FILE)
        self.ts1 = self._configure_ts('shmd_ts1')
        self.ts2 = self._configure_ts('shmd_ts2')
        self.ts3 = self._configure_ts('shmd_ts3')
        self._add_diags_log_assertions()

    def _setup_shared_state(self):
        Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', self.TS_PID_SCRIPT))
        Test.Setup.Copy(os.path.join(Test.TestDirectory, self.POKE_SCRIPT))

        shared_storage_dir = os.path.join(Test.RunDirectory, 'shared-storage')
        os.makedirs(shared_storage_dir, exist_ok=True)
        self._shared_storage_path = os.path.join(shared_storage_dir, 'disk.img')
        with open(self._shared_storage_path, 'ab') as f:
            f.truncate(self.SHARED_DISK_SIZE_BYTES)

        # macOS PSHMNAMLEN is 31 chars incl. '/'; 'd' = dir-invalid variant.
        # (This test is Linux-only, but keep the prefix short for consistency.)
        self._shm_prefix = f'/cshmd-{os.getpid() % 100000}-'
        # A single span with a single volume yields exactly one stripe, so its
        # segment is index 0. On Linux each segment is a file under /dev/shm by the
        # same name (sans the leading '/').
        self._stripe_file = '/dev/shm/' + self._shm_prefix.lstrip('/') + 's0'

    def _configure_ts(self, name):
        ts = Test.MakeATSProcess(name)
        # An absolute span path keeps the span independent of MakeATSProcess's
        # per-instance STORAGEDIR, so every ts shares the same on-disk cache and
        # therefore the same stripe geometry (hence the same shm identity).
        ts.Disk.storage_config.AddLine(f'{self._shared_storage_path} {self.SHARED_DISK_SIZE_BYTES}')
        ts.Disk.volume_config.AddLine('volume=1 scheme=http size=100%')
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
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.http_port}/')
        return ts

    def _add_reject_assertions(self, ts, label):
        # The shm segments themselves are still attached -- the control segment is
        # untouched and clean, so this is specifically the per-stripe directory gate.
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: attaching up to \d+ stripes \(fast restart', f'{label} should attach the existing control segment')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: attached stripe \S+ \(\d+ bytes\) for key=', f'{label} should attach the existing stripe segment')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r"shm directory invalid for '.+'; falling back to disk read", f'{label} must reject the tampered shm directory')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'attaching cached directory from shm for', f'{label} must not fast-attach the tampered directory')
        # The rejection is per-stripe: the control segment stays valid, so none of
        # the whole-segment drop reasons should appear.
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: (schema|ABI) mismatch', f'{label} should reject the directory, not the control segment')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'cache shm: previous run did not shutdown cleanly', f'{label} should see the shm marked clean')

    def _add_diags_log_assertions(self):
        # ts1 cold start, clean shutdown -- a valid, clean stripe segment to tamper with.
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: creating fresh control segment', 'ts1 should create a fresh shm control segment on first start')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: created stripe \S+ \(\d+ bytes\) for key=', 'ts1 should create the shm-backed stripe segment')
        self.ts1.Disk.diags_log.Content += Testers.ContainsExpression(
            r'cache shm: marking clean shutdown', 'ts1 should mark the shm clean before exit')
        self.ts1.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'shm directory invalid for', 'ts1 has no shm directory to reject on cold start')

        self._add_reject_assertions(self.ts2, 'ts2 (write_pos)')
        self._add_reject_assertions(self.ts3, 'ts3 (freelist[0])')

    def _fill(self):
        tr = Test.AddTestRun('Cold-start ts1 and cache an object')
        tr.AddVerifierClientProcess(
            'shmd-fill-client', self.REPLAY_FILE, http_ports=[self.ts1.Variables.port], keys='fill', other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts1)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts1

    def _clean_shutdown(self, ts, name):
        tr = Test.AddTestRun(f'Drain and clean-shutdown {name}')
        tr.Processes.Default.Env = ts.Env
        tr.Processes.Default.Command = (
            f'traffic_ctl server drain && sleep 1 && '
            f'{sys.executable} ./{self.TS_PID_SCRIPT} {name} --signal TERM && sleep 3')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server

    def _poke(self, description, offset, hex_bytes):
        # The previous ts is dead; the stripe segment is just a file now.
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Command = (f'{sys.executable} ./{self.POKE_SCRIPT} {self._stripe_file} {offset} {hex_bytes}')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server

    def _verify_reject(self, ts, key, description):
        tr = Test.AddTestRun(description)
        tr.AddVerifierClientProcess(
            f'shmd-{key}-client', self.REPLAY_FILE, http_ports=[ts.Variables.port], keys=key, other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = ts

    def _cleanup_shm(self):
        tr = Test.AddTestRun('Unlink the test shm segments')
        tr.Processes.Default.Env = self.ts3.Env
        tr.Processes.Default.Command = f'traffic_ctl cache shm clear --prefix {self._shm_prefix}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
            'Invalid argument', 'clear must skip tombstoned slots, not fail on them')

    def run(self):
        self._fill()
        self._clean_shutdown(self.ts1, 'shmd_ts1')

        self._poke('Tamper write_pos in the in-shm stripe header', self.WRITE_POS_OFFSET, self.BOGUS_WRITE_POS_LE_HEX)
        self._verify_reject(self.ts2, 'hit_write_pos', 'Start ts2; an out-of-range write_pos is rejected and rebuilt from disk')
        self._clean_shutdown(self.ts2, 'shmd_ts2')

        self._poke('Tamper freelist[0] in the in-shm stripe header', self.FREELIST_0_OFFSET, self.BOGUS_FREELIST_LE_HEX)
        self._verify_reject(self.ts3, 'hit_freelist', 'Start ts3; an out-of-range freelist head is rejected and rebuilt from disk')
        self._clean_shutdown(self.ts3, 'shmd_ts3')

        self._cleanup_shm()


CacheShmDirInvalidTest().run()
