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


def test_cache_shm_bad_disk_dropped(ats_factory: ATSFactory, curl: Curl) -> None:
    """Dropping a disk attaches its surviving stripe and reclaims the orphan.

    The first instance cleanly shuts down with two spans. The second keeps the
    same shm prefix but advertises only the surviving span, so it must partially
    attach instead of rebuilding the entire control segment.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    prefix = shm_prefix("bd")
    path = f"/cache/40/{uuid.uuid4()}"
    disk_a = make_disk(ats_factory.run_directory, "disk_a.img")
    disk_b = make_disk(ats_factory.run_directory, "disk_b.img")
    ts1 = configure_shm_ats(
        ats_factory,
        "shmbd_ts1",
        prefix,
        [disk_a, disk_b],
        debug_tags="cache_shm|cache_init",
    )
    ts2 = configure_shm_ats(
        ats_factory,
        "shmbd_ts2",
        prefix,
        [disk_a],
        debug_tags="cache_shm|cache_init",
    )
    ts1.start()
    get_200(curl, ts1, path)
    clean_shutdown(ts1)
    result = ts1.traffic_ctl("cache", "shm", "status", "--prefix", prefix)

    assert result.returncode == 0, result.output
    assert_matches_gold(result.stdout, Path(__file__).parent / "gold/cache_shm_state_after_shutdown.gold")
    assert "untrusted" not in result.stdout
    ts2.start()
    get_200(curl, ts2, path)
    clean_shutdown(ts2)
    assert_log(
        ts1,
        contains=(
            r"cache shm: creating fresh control segment",
            r"created stripe \S+ \(\d+ bytes\) for key=",
            r"cache shm: marking clean shutdown",
        ),
    )
    assert_log(
        ts2,
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
    clear_shm(ts2, prefix)
