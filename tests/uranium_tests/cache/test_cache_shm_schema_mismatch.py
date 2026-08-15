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


class CacheShmSchemaMismatchScenario:
    """A mismatched control schema is dropped and recreated.

    The first instance leaves a clean control segment. Changing its on-disk
    schema field then verifies that the next instance rejects only for the
    schema mismatch and rebuilds safely from disk.
    """

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.prefix = shm_prefix("x")
        self.path = f"/cache/40/{uuid.uuid4()}"

    def _check_requirements(self) -> None:
        if platform.system() != "Linux":
            pytest.skip("shm byte-poke gates need Linux /dev/shm")

    def _configure_storage(self) -> None:
        self.disk = make_disk(self.ats_factory.run_directory, "disk.img")

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(self.ats_factory, "shmx_ts1", self.prefix, [self.disk])
        self.ts2 = configure_shm_ats(self.ats_factory, "shmx_ts2", self.prefix, [self.disk])

    def _create_clean_shared_memory(self) -> None:
        self.ts1.start()
        get_200(self.curl, self.ts1, self.path)
        clean_shutdown(self.ts1)

    def _corrupt_control_schema(self) -> None:
        control_file = Path("/dev/shm") / f"{self.prefix.lstrip('/')}control"
        result = self.ts1.run(
            sys.executable,
            Path(__file__).parent / "shm_poke.py",
            control_file,
            "8",
            "09000000",
        )

        assert result.returncode == 0, result.output

    def _restart_with_mismatched_schema(self) -> None:
        self.ts2.start()
        get_200(self.curl, self.ts2, self.path)
        clean_shutdown(self.ts2)

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
                r"cache shm: schema mismatch \(\d+ vs \d+\), dropping",
                r"cache shm: creating fresh control segment",
            ),
            excludes=(
                r"\(fast restart, recovery skipped\)",
                r"cache shm: previous run did not shutdown cleanly",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts2, self.prefix)

    def run(self) -> None:
        self._check_requirements()
        self._configure_storage()
        self._configure_traffic_servers()
        self._create_clean_shared_memory()
        self._corrupt_control_schema()
        self._restart_with_mismatched_schema()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_schema_mismatch(ats_factory: ATSFactory, curl: Curl) -> None:
    CacheShmSchemaMismatchScenario(ats_factory, curl).run()
