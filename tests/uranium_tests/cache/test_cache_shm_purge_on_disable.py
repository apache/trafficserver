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


def test_cache_shm_purge_on_disable(ats_factory: ATSFactory) -> None:
    """purge_stale_on_start removes leftover shm only when requested.

    Independent prefixes cover positive purge, configured retention, and a
    quiet no-op when no control segment exists. ``traffic_ctl`` checks the shm
    state before and after each disabled instance starts.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    def _shared_memory_status(ats: ATS, prefix: str, *, present: bool) -> str:
        """shared memory status.

        :param ats: Traffic Server instance configured or queried by this step.
        :param prefix: Prefix used by this test step.
        :param present: Present used by this test step.
        """
        result = ats.traffic_ctl("cache", "shm", "status", "--prefix", prefix)
        control_name = prefix + "control"
        if present:
            assert result.returncode == 0, result.output
            assert re.search(r"Control segment:\s+" + re.escape(control_name), result.stdout)
            return result.stdout

        assert result.returncode == 2, result.output
        assert re.search(r"control segment '" + re.escape(control_name) + r"' not found", result.stderr)
        return result.stderr

    purge_prefix = shm_prefix("p")
    keep_prefix = shm_prefix("k")
    noop_prefix = shm_prefix("n")
    purge_disk = make_disk(ats_factory.run_directory, "disk_p.img")
    keep_disk = make_disk(ats_factory.run_directory, "disk_k.img")
    noop_disk = make_disk(ats_factory.run_directory, "disk_n.img")
    seed_purge = configure_shm_ats(ats_factory, "cshm_seed_p", purge_prefix, [purge_disk])
    seed_keep = configure_shm_ats(ats_factory, "cshm_seed_k", keep_prefix, [keep_disk])
    run_purge = configure_shm_ats(
        ats_factory,
        "cshm_run_p",
        purge_prefix,
        [purge_disk],
        enabled=False,
        purge=True,
    )
    run_keep = configure_shm_ats(
        ats_factory,
        "cshm_run_k",
        keep_prefix,
        [keep_disk],
        enabled=False,
        purge=False,
    )
    run_noop = configure_shm_ats(
        ats_factory,
        "cshm_run_n",
        noop_prefix,
        [noop_disk],
        enabled=False,
        purge=True,
    )
    seed_purge.start()
    _shared_memory_status(seed_purge, purge_prefix, present=True)
    clean_shutdown(seed_purge)
    clean_state = _shared_memory_status(seed_purge, purge_prefix, present=True)

    assert re.search(r"clean_shutdown:\s+1 \(clean\)", clean_state)
    run_purge.start()
    _shared_memory_status(run_purge, purge_prefix, present=False)
    seed_keep.start()
    _shared_memory_status(seed_keep, keep_prefix, present=True)
    clean_shutdown(seed_keep)
    run_keep.start()
    _shared_memory_status(run_keep, keep_prefix, present=True)
    run_noop.start()
    _shared_memory_status(run_noop, noop_prefix, present=False)
    for seed in (seed_purge, seed_keep):
        assert_log(
            seed,
            contains=(
                r"cache shm: creating fresh control segment",
                r"cache shm: marking clean shutdown",
            ),
        )
    assert_log(
        run_purge,
        contains=(r"cache shm: purged stale segments while disabled \(removed [1-9]",),
    )
    assert_log(run_keep, excludes=(r"cache shm: purged stale segments",))
    assert_log(
        run_noop,
        excludes=(
            r"cache shm: purged stale segments",
            r"cache shm: cannot open control segment",
        ),
    )
    clear_shm(run_keep, purge_prefix, keep_prefix, noop_prefix)
