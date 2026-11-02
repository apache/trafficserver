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


def test_cache_shm_schema_mismatch(ats_factory: ATSFactory, curl: Curl) -> None:
    """A mismatched control schema is dropped and recreated.

    The first instance leaves a clean control segment. Changing its on-disk
    schema field then verifies that the next instance rejects only for the
    schema mismatch and rebuilds safely from disk.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    prefix = shm_prefix("x")
    path = f"/cache/40/{uuid.uuid4()}"
    if platform.system() != "Linux":
        pytest.skip("shm byte-poke gates need Linux /dev/shm")
    disk = make_disk(ats_factory.run_directory, "disk.img")
    ts1 = configure_shm_ats(ats_factory, "shmx_ts1", prefix, [disk])
    ts2 = configure_shm_ats(ats_factory, "shmx_ts2", prefix, [disk])
    ts1.start()
    get_200(curl, ts1, path)
    clean_shutdown(ts1)
    control_file = Path("/dev/shm") / f"{prefix.lstrip('/')}control"
    result = ts1.run(
        sys.executable,
        Path(__file__).parent / "shm_poke.py",
        control_file,
        "8",
        "09000000",
    )

    assert result.returncode == 0, result.output
    ts2.start()
    get_200(curl, ts2, path)
    clean_shutdown(ts2)
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
            r"cache shm: schema mismatch \(\d+ vs \d+\), dropping",
            r"cache shm: creating fresh control segment",
        ),
        excludes=(
            r"\(fast restart, recovery skipped\)",
            r"cache shm: previous run did not shutdown cleanly",
        ),
    )
    clear_shm(ts2, prefix)
