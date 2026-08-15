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


class CacheShmControlSizeMismatchScenario:
    """A foreign-size control segment is dropped, recreated, then reused.

    Growing the Linux shm file models an upgrade that changed
    ``sizeof(CacheShmControl)``. The second instance must heal the segment, and
    the third proves that the healed segment is valid for fast restart.
    """

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.prefix = shm_prefix("z")
        self.path = f"/cache/40/{uuid.uuid4()}"

    def _check_requirements(self) -> None:
        if platform.system() != "Linux":
            pytest.skip("shm byte-poke gates need Linux /dev/shm")

    def _configure_storage(self) -> None:
        self.disk = make_disk(self.ats_factory.run_directory, "disk.img")

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(self.ats_factory, "shmz_ts1", self.prefix, [self.disk])
        self.ts2 = configure_shm_ats(self.ats_factory, "shmz_ts2", self.prefix, [self.disk])
        self.ts3 = configure_shm_ats(self.ats_factory, "shmz_ts3", self.prefix, [self.disk])

    def _create_clean_shared_memory(self) -> None:
        self.ts1.start()
        get_200(self.curl, self.ts1, self.path)
        clean_shutdown(self.ts1)

    def _grow_control_segment(self) -> None:
        control_file = Path("/dev/shm") / f"{self.prefix.lstrip('/')}control"
        result = self.ts1.run(
            sys.executable,
            Path(__file__).parent / "shm_poke.py",
            control_file,
            str(1024 * 1024),
            "00",
        )

        assert result.returncode == 0, result.output

    def _heal_control_segment(self) -> None:
        self.ts2.start()
        get_200(self.curl, self.ts2, self.path)
        clean_shutdown(self.ts2)

    def _reuse_healed_control_segment(self) -> None:
        self.ts3.start()
        get_200(self.curl, self.ts3, self.path)
        clean_shutdown(self.ts3)

    def _verify_restart_logs(self) -> None:
        assert_log(
            self.ts1,
            contains=(
                r"cache shm: creating fresh control segment",
                r"cache shm: marking clean shutdown",
            ),
        )
        assert_log(
            self.ts2,
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
            self.ts3,
            contains=(
                r"cache shm: attaching up to \d+ stripes \(fast restart",
                r"attaching cached directory from shm for '.+' \(fast restart",
            ),
            excludes=(
                r"cache shm: control segment \S+ is \d+ bytes",
                r"cache shm: creating fresh control segment",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts3, self.prefix)

    def run(self) -> None:
        self._check_requirements()
        self._configure_storage()
        self._configure_traffic_servers()
        self._create_clean_shared_memory()
        self._grow_control_segment()
        self._heal_control_segment()
        self._reuse_healed_control_segment()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_control_size_mismatch(ats_factory: ATSFactory, curl: Curl) -> None:
    CacheShmControlSizeMismatchScenario(ats_factory, curl).run()
