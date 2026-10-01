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

import uuid

from tools.uranium.services import ATSFactory, Curl
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, get_200, make_disk, shm_prefix


def test_cache_shm_storage_mismatch(ats_factory: ATSFactory, curl: Curl) -> None:
    """A changed storage path creates a fresh stripe and reclaims the old one.

    Both instances share a control prefix but point at different disk paths.
    The second must retain the control segment in partial-attach mode without
    attaching a stripe directory that describes the first layout.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    prefix = shm_prefix("s")
    path = f"/cache/40/{uuid.uuid4()}"
    disk_a = make_disk(ats_factory.run_directory, "disk_a.img")
    disk_b = make_disk(ats_factory.run_directory, "disk_b.img")
    ts1 = configure_shm_ats(ats_factory, "shms_ts1", prefix, [disk_a])
    ts2 = configure_shm_ats(ats_factory, "shms_ts2", prefix, [disk_b])
    ts1.start()
    get_200(curl, ts1, path)
    clean_shutdown(ts1)
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
            r"created stripe \S+ \(\d+ bytes\) for key=",
            r"cache shm: reclaiming orphaned stripe segment",
            r"reclaimed \d+ orphaned stripe segment\(s\) after attach",
        ),
        excludes=(
            r"attached stripe \S+ \(\d+ bytes\) for key=",
            r"cache shm: creating fresh control segment",
            r"cache shm: (schema|ABI) mismatch",
            r"cache shm: previous run did not shutdown cleanly",
        ),
    )
    clear_shm(ts2, prefix)
