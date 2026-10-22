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
import platform
import sys
import uuid

import pytest

from tools.uranium.services import ATSFactory, Curl
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, get_200, make_disk, shm_prefix


def test_cache_shm_control_size_mismatch(ats_factory: ATSFactory, curl: Curl) -> None:
    """A foreign-size control segment is dropped, recreated, then reused.

    Growing the Linux shm file models an upgrade that changed
    ``sizeof(CacheShmControl)``. The second instance must heal the segment, and
    the third proves that the healed segment is valid for fast restart.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    prefix = shm_prefix("z")
    path = f"/cache/40/{uuid.uuid4()}"
    if platform.system() != "Linux":
        pytest.skip("shm byte-poke gates need Linux /dev/shm")
    disk = make_disk(ats_factory.run_directory, "disk.img")
    ts1 = configure_shm_ats(ats_factory, "shmz_ts1", prefix, [disk])
    ts2 = configure_shm_ats(ats_factory, "shmz_ts2", prefix, [disk])
    ts3 = configure_shm_ats(ats_factory, "shmz_ts3", prefix, [disk])
    ts1.start()
    get_200(curl, ts1, path)
    clean_shutdown(ts1)
    control_file = Path("/dev/shm") / f"{prefix.lstrip('/')}control"
    result = ts1.run(
        sys.executable,
        Path(__file__).parent / "shm_poke.py",
        control_file,
        str(1024 * 1024),
        "00",
    )

    assert result.returncode == 0, result.output
    ts2.start()
    get_200(curl, ts2, path)
    clean_shutdown(ts2)
    ts3.start()
    get_200(curl, ts3, path)
    clean_shutdown(ts3)
    assert_log(
        ts1,
        contains=(
            r"cache shm: creating fresh control segment",
            r"cache shm: marking clean shutdown",
        ),
    )
    assert_log(
        ts2,
        contains=(
            r"cache shm: control segment \S+ is \d+ bytes, not this build's \d+; dropping it",
            r"cache shm: creating fresh control segment",
        ),
        excludes=(
            r"cache shm: failed to create control segment",
            r"\(fast restart, recovery skipped\)",
        ),
    )
    assert_log(
        ts3,
        contains=(
            r"cache shm: attaching up to \d+ stripes \(fast restart",
            r"attaching cached directory from shm for '.+' \(fast restart",
        ),
        excludes=(
            r"cache shm: control segment \S+ is \d+ bytes",
            r"cache shm: creating fresh control segment",
        ),
    )
    clear_shm(ts3, prefix)
