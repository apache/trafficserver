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

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, make_disk, shm_prefix

REPLAY = "replay/cache-shm-dir-invalid.replay.yaml"


class CacheShmDirectoryInvalidScenario:
    """Out-of-range shm directory fields fall back to disk recovery.

    The test separately corrupts ``write_pos`` and ``freelist[0]`` in a clean
    stripe segment. Each restart may attach the segment itself, but must reject
    the unsafe directory contents before they can drive out-of-bounds disk I/O.
    """

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self.ats_factory = ats_factory
        self.services = services
        self.prefix = shm_prefix("d")
        self.stripe_file = Path("/dev/shm") / f"{self.prefix.lstrip('/')}s0"
        self.poke_script = Path(__file__).parent / "shm_poke.py"

    def _check_requirements(self) -> None:
        if platform.system() != "Linux":
            pytest.skip("shm byte-poke gates need Linux /dev/shm")

    def _configure_storage(self) -> None:
        self.disk = make_disk(self.ats_factory.run_directory, "disk.img")

    def _configure_origin(self) -> None:
        self.origin = self.services.verifier_server("shmd-origin", REPLAY)

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(
            self.ats_factory,
            "shmd_ts1",
            self.prefix,
            [self.disk],
            origin_port=self.origin.http_port,
        )
        self.ts2 = configure_shm_ats(
            self.ats_factory,
            "shmd_ts2",
            self.prefix,
            [self.disk],
            origin_port=self.origin.http_port,
        )
        self.ts3 = configure_shm_ats(
            self.ats_factory,
            "shmd_ts3",
            self.prefix,
            [self.disk],
            origin_port=self.origin.http_port,
        )

    def _start_origin(self) -> None:
        self.origin.start()

    def _fill_cache_and_cleanly_shutdown(self) -> None:
        self.ts1.start()
        result = self.services.verifier_client(
            "shmd-fill-client",
            REPLAY,
            http_ports=[self.ts1.http_port],
            keys="fill",
            other_args="--thread-limit 1",
        ).run()

        assert result.returncode == 0, result.output
        clean_shutdown(self.ts1)

    def _corrupt_write_position(self) -> None:
        result = self.ts1.run(sys.executable, self.poke_script, self.stripe_file, "16", "ffffffffffff0000")

        assert result.returncode == 0, result.output

    def _verify_write_position_falls_back_to_disk(self) -> None:
        self.ts2.start()
        result = self.services.verifier_client(
            "shmd-write-pos-client",
            REPLAY,
            http_ports=[self.ts2.http_port],
            keys="hit_write_pos",
            other_args="--thread-limit 1",
        ).run()

        assert result.returncode == 0, result.output
        clean_shutdown(self.ts2)

    def _corrupt_freelist(self) -> None:
        result = self.ts2.run(sys.executable, self.poke_script, self.stripe_file, "72", "ffff")

        assert result.returncode == 0, result.output

    def _verify_freelist_falls_back_to_disk(self) -> None:
        self.ts3.start()
        result = self.services.verifier_client(
            "shmd-freelist-client",
            REPLAY,
            http_ports=[self.ts3.http_port],
            keys="hit_freelist",
            other_args="--thread-limit 1",
        ).run()

        assert result.returncode == 0, result.output
        clean_shutdown(self.ts3)

    def _assert_rejected_directory(self, ats: ATS) -> None:
        assert_log(
            ats,
            contains=(
                r"cache shm: attaching up to \d+ stripes \(fast restart",
                r"cache shm: attached stripe \S+ \(\d+ bytes\) for key=",
                r"shm directory invalid for '.+'; falling back to disk read",
            ),
            excludes=(
                r"attaching cached directory from shm for",
                r"cache shm: (schema|ABI) mismatch",
                r"cache shm: previous run did not shutdown cleanly",
            ),
        )

    def _verify_restart_logs(self) -> None:
        assert_log(
            self.ts1,
            contains=(
                r"cache shm: creating fresh control segment",
                r"cache shm: created stripe \S+ \(\d+ bytes\) for key=",
                r"cache shm: marking clean shutdown",
            ),
            excludes=(r"shm directory invalid for",),
        )
        self._assert_rejected_directory(self.ts2)
        self._assert_rejected_directory(self.ts3)

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts3, self.prefix)

    def run(self) -> None:
        self._check_requirements()
        self._configure_storage()
        self._configure_origin()
        self._configure_traffic_servers()
        self._start_origin()
        self._fill_cache_and_cleanly_shutdown()
        self._corrupt_write_position()
        self._verify_write_position_falls_back_to_disk()
        self._corrupt_freelist()
        self._verify_freelist_falls_back_to_disk()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_dir_invalid(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    CacheShmDirectoryInvalidScenario(ats_factory, services).run()
