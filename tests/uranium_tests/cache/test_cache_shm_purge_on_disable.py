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

import re

from tools.uranium.services import ATS, ATSFactory
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, make_disk, shm_prefix


class CacheShmPurgeOnDisableScenario:
    """purge_stale_on_start removes leftover shm only when requested.

    Independent prefixes cover positive purge, configured retention, and a
    quiet no-op when no control segment exists. ``traffic_ctl`` checks the shm
    state before and after each disabled instance starts.
    """

    def __init__(self, ats_factory: ATSFactory) -> None:
        self.ats_factory = ats_factory
        self.purge_prefix = shm_prefix("p")
        self.keep_prefix = shm_prefix("k")
        self.noop_prefix = shm_prefix("n")

    def _configure_storage(self) -> None:
        self.purge_disk = make_disk(self.ats_factory.run_directory, "disk_p.img")
        self.keep_disk = make_disk(self.ats_factory.run_directory, "disk_k.img")
        self.noop_disk = make_disk(self.ats_factory.run_directory, "disk_n.img")

    def _configure_traffic_servers(self) -> None:
        self.seed_purge = configure_shm_ats(self.ats_factory, "cshm_seed_p", self.purge_prefix, [self.purge_disk])
        self.seed_keep = configure_shm_ats(self.ats_factory, "cshm_seed_k", self.keep_prefix, [self.keep_disk])
        self.run_purge = configure_shm_ats(
            self.ats_factory,
            "cshm_run_p",
            self.purge_prefix,
            [self.purge_disk],
            enabled=False,
            purge=True,
        )
        self.run_keep = configure_shm_ats(
            self.ats_factory,
            "cshm_run_k",
            self.keep_prefix,
            [self.keep_disk],
            enabled=False,
            purge=False,
        )
        self.run_noop = configure_shm_ats(
            self.ats_factory,
            "cshm_run_n",
            self.noop_prefix,
            [self.noop_disk],
            enabled=False,
            purge=True,
        )

    def _shared_memory_status(self, ats: ATS, prefix: str, *, present: bool) -> str:
        result = ats.traffic_ctl("cache", "shm", "status", "--prefix", prefix)
        control_name = prefix + "control"
        if present:
            assert result.returncode == 0, result.output
            assert re.search(r"Control segment:\s+" + re.escape(control_name), result.stdout)
            return result.stdout

        assert result.returncode == 2, result.output
        assert re.search(r"control segment '" + re.escape(control_name) + r"' not found", result.stderr)
        return result.stderr

    def _seed_shared_memory_for_purge(self) -> None:
        self.seed_purge.start()
        self._shared_memory_status(self.seed_purge, self.purge_prefix, present=True)
        clean_shutdown(self.seed_purge)
        clean_state = self._shared_memory_status(self.seed_purge, self.purge_prefix, present=True)

        assert re.search(r"clean_shutdown:\s+1 \(clean\)", clean_state)

    def _purge_shared_memory_while_disabled(self) -> None:
        self.run_purge.start()
        self._shared_memory_status(self.run_purge, self.purge_prefix, present=False)

    def _retain_shared_memory_while_disabled(self) -> None:
        self.seed_keep.start()
        self._shared_memory_status(self.seed_keep, self.keep_prefix, present=True)
        clean_shutdown(self.seed_keep)
        self.run_keep.start()
        self._shared_memory_status(self.run_keep, self.keep_prefix, present=True)

    def _purge_missing_shared_memory(self) -> None:
        self.run_noop.start()
        self._shared_memory_status(self.run_noop, self.noop_prefix, present=False)

    def _verify_logs(self) -> None:
        for seed in (self.seed_purge, self.seed_keep):
            assert_log(
                seed,
                contains=(
                    r"cache shm: creating fresh control segment",
                    r"cache shm: marking clean shutdown",
                ),
            )
        assert_log(
            self.run_purge,
            contains=(r"cache shm: purged stale segments while disabled \(removed [1-9]",),
        )
        assert_log(self.run_keep, excludes=(r"cache shm: purged stale segments",))
        assert_log(
            self.run_noop,
            excludes=(
                r"cache shm: purged stale segments",
                r"cache shm: cannot open control segment",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.run_keep, self.purge_prefix, self.keep_prefix, self.noop_prefix)

    def run(self) -> None:
        self._configure_storage()
        self._configure_traffic_servers()
        self._seed_shared_memory_for_purge()
        self._purge_shared_memory_while_disabled()
        self._retain_shared_memory_while_disabled()
        self._purge_missing_shared_memory()
        self._verify_logs()
        self._clear_shared_memory()


def test_cache_shm_purge_on_disable(ats_factory: ATSFactory) -> None:
    CacheShmPurgeOnDisableScenario(ats_factory).run()
