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

from tools.uranium.services import ATSFactory, ServiceFactory
from uranium_tests.cache.shm_helpers import assert_log, clean_shutdown, clear_shm, configure_shm_ats, make_disk, shm_prefix

REPLAY = "replay/cache-shm-fast-restart.replay.yaml"


class CacheShmFastRestartScenario:
    """A clean restart attaches the cache directory from shared memory.

    The first instance fills the shared disk and marks shm clean during
    shutdown. The second uses the same disk and prefix; its replay contains a
    sentinel origin response so only a cache hit can pass.
    """

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self.ats_factory = ats_factory
        self.services = services
        self.prefix = shm_prefix("")

    def _configure_storage(self) -> None:
        self.disk = make_disk(self.ats_factory.run_directory, "disk.img")

    def _configure_origin(self) -> None:
        self.origin = self.services.verifier_server("shm-origin", REPLAY)

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(
            self.ats_factory,
            "shm_ts1",
            self.prefix,
            [self.disk],
            origin_port=self.origin.http_port,
        )
        self.ts2 = configure_shm_ats(
            self.ats_factory,
            "shm_ts2",
            self.prefix,
            [self.disk],
            origin_port=self.origin.http_port,
        )

    def _start_origin(self) -> None:
        self.origin.start()

    def _fill_cache_and_cleanly_shutdown(self) -> None:
        self.ts1.start()
        result = self.services.verifier_client(
            "shm-fill-client",
            REPLAY,
            http_ports=[self.ts1.http_port],
            keys="fill",
            other_args="--thread-limit 1",
        ).run()

        assert result.returncode == 0, result.output
        clean_shutdown(self.ts1)

    def _restart_and_verify_cache_hit(self) -> None:
        self.ts2.start()
        result = self.services.verifier_client(
            "shm-hit-client",
            REPLAY,
            http_ports=[self.ts2.http_port],
            keys="hit",
            other_args="--thread-limit 1",
        ).run()

        assert result.returncode == 0, result.output
        clean_shutdown(self.ts2)

    def _verify_restart_logs(self) -> None:
        assert_log(
            self.ts1,
            contains=(
                r"cache shm: creating fresh control segment",
                r"cache shm: created stripe \S+ \(\d+ bytes\) for key=",
                r"cache shm: marking clean shutdown",
            ),
            excludes=(
                r"cache shm: (schema|ABI) mismatch",
                r"cache shm: previous run did not shutdown cleanly",
                r"cache shm: stripe \S+ size mismatch",
            ),
        )
        assert_log(
            self.ts2,
            contains=(
                r"cache shm: attaching up to \d+ stripes \(fast restart",
                r"cache shm: attached stripe \S+ \(\d+ bytes\) for key=",
                r"attaching cached directory from shm for '.+' \(fast restart",
            ),
            excludes=(
                r"cache shm: creating fresh control segment",
                r"cache shm: (schema|ABI) mismatch",
                r"cache shm: previous run did not shutdown cleanly",
                r"shm directory invalid for",
                r"cache shm: stripe \S+ size mismatch",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts2, self.prefix)

    def run(self) -> None:
        self._configure_storage()
        self._configure_origin()
        self._configure_traffic_servers()
        self._start_origin()
        self._fill_cache_and_cleanly_shutdown()
        self._restart_and_verify_cache_hit()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_fast_restart(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    CacheShmFastRestartScenario(ats_factory, services).run()
