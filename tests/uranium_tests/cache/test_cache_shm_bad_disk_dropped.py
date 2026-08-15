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

from pathlib import Path
import uuid

from tools.uranium.services import ATSFactory, Curl, assert_matches_gold
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, get_200, make_disk, shm_prefix


class CacheShmBadDiskDroppedScenario:
    """Dropping a disk attaches its surviving stripe and reclaims the orphan.

    The first instance cleanly shuts down with two spans. The second keeps the
    same shm prefix but advertises only the surviving span, so it must partially
    attach instead of rebuilding the entire control segment.
    """

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.prefix = shm_prefix("bd")
        self.path = f"/cache/40/{uuid.uuid4()}"

    def _configure_storage(self) -> None:
        self.disk_a = make_disk(self.ats_factory.run_directory, "disk_a.img")
        self.disk_b = make_disk(self.ats_factory.run_directory, "disk_b.img")

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(
            self.ats_factory,
            "shmbd_ts1",
            self.prefix,
            [self.disk_a, self.disk_b],
            debug_tags="cache_shm|cache_init",
        )
        self.ts2 = configure_shm_ats(
            self.ats_factory,
            "shmbd_ts2",
            self.prefix,
            [self.disk_a],
            debug_tags="cache_shm|cache_init",
        )

    def _populate_cache_and_cleanly_shutdown(self) -> None:
        self.ts1.start()
        get_200(self.curl, self.ts1, self.path)
        clean_shutdown(self.ts1)

    def _verify_clean_shared_memory_state(self) -> None:
        result = self.ts1.traffic_ctl("cache", "shm", "status", "--prefix", self.prefix)

        assert result.returncode == 0, result.output
        assert_matches_gold(result.stdout, Path(__file__).parent / "gold/cache_shm_state_after_shutdown.gold")
        assert "untrusted" not in result.stdout

    def _restart_without_the_second_disk(self) -> None:
        self.ts2.start()
        get_200(self.curl, self.ts2, self.path)
        clean_shutdown(self.ts2)

    def _verify_restart_logs(self) -> None:
        assert_log(
            self.ts1,
            contains=(
                r"cache shm: creating fresh control segment",
                r"created stripe \S+ \(\d+ bytes\) for key=",
                r"cache shm: marking clean shutdown",
            ),
        )
        assert_log(
            self.ts2,
            contains=(
                r"attaching up to \d+ stripes \(fast restart, partial -- storage changed\)",
                r"attached stripe \S+ \(\d+ bytes\) for key=",
                r"cache shm: reclaiming orphaned stripe segment",
                r"reclaimed \d+ orphaned stripe segment\(s\) after attach",
            ),
            excludes=(
                r"cache shm: creating fresh control segment",
                r"cache shm: previous run did not shutdown cleanly",
                r"cache shm: (schema|ABI) mismatch",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts2, self.prefix)

    def run(self) -> None:
        self._configure_storage()
        self._configure_traffic_servers()
        self._populate_cache_and_cleanly_shutdown()
        self._verify_clean_shared_memory_state()
        self._restart_without_the_second_disk()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_bad_disk_dropped(ats_factory: ATSFactory, curl: Curl) -> None:
    CacheShmBadDiskDroppedScenario(ats_factory, curl).run()
