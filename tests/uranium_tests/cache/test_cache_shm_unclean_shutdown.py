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


def test_cache_shm_unclean_shutdown(ats_factory: ATSFactory, curl: Curl) -> None:
    """A dirty shm directory is dropped after SIGKILL.

    SIGKILL bypasses the shutdown hook, leaving ``clean_shutdown`` unset. The
    next instance must drop that directory rather than trust entries that may
    refer to cache writes which never reached disk.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    prefix = shm_prefix("u")
    path = f"/cache/40/{uuid.uuid4()}"
    disk = make_disk(ats_factory.run_directory, "disk.img")
    ts1 = configure_shm_ats(ats_factory, "shmu_ts1", prefix, [disk])
    ts2 = configure_shm_ats(ats_factory, "shmu_ts2", prefix, [disk])
    ts1.start()
    get_200(curl, ts1, path)
    ts1.kill()
    ts2.start()
    get_200(curl, ts2, path)
    clean_shutdown(ts2)
    assert_log(
        ts1,
        contains=(r"cache shm: creating fresh control segment",),
        excludes=(r"cache shm: marking clean shutdown",),
    )
    assert_log(
        ts2,
        contains=(
            r"cache shm: previous run did not shutdown cleanly, dropping",
            r"cache shm: creating fresh control segment",
        ),
        excludes=(
            r"\(fast restart, recovery skipped\)",
            r"cache shm: attaching up to \d+ stripes \(fast restart",
        ),
    )
    clear_shm(ts2, prefix)
