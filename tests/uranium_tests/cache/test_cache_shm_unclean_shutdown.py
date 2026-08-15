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


class CacheShmUncleanShutdownScenario:
    """A dirty shm directory is dropped after SIGKILL.

    SIGKILL bypasses the shutdown hook, leaving ``clean_shutdown`` unset. The
    next instance must drop that directory rather than trust entries that may
    refer to cache writes which never reached disk.
    """

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.prefix = shm_prefix("u")
        self.path = f"/cache/40/{uuid.uuid4()}"

    def _configure_storage(self) -> None:
        self.disk = make_disk(self.ats_factory.run_directory, "disk.img")

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(self.ats_factory, "shmu_ts1", self.prefix, [self.disk])
        self.ts2 = configure_shm_ats(self.ats_factory, "shmu_ts2", self.prefix, [self.disk])

    def _populate_cache_and_kill_traffic_server(self) -> None:
        self.ts1.start()
        get_200(self.curl, self.ts1, self.path)
        self.ts1.kill()

    def _restart_after_unclean_shutdown(self) -> None:
        self.ts2.start()
        get_200(self.curl, self.ts2, self.path)
        clean_shutdown(self.ts2)

    def _verify_restart_logs(self) -> None:
        assert_log(
            self.ts1,
            contains=(r"cache shm: creating fresh control segment",),
            excludes=(r"cache shm: marking clean shutdown",),
        )
        assert_log(
            self.ts2,
            contains=(
                r"cache shm: previous run did not shutdown cleanly, dropping",
                r"cache shm: creating fresh control segment",
            ),
            excludes=(
                r"\(fast restart, recovery skipped\)",
                r"cache shm: attaching up to \d+ stripes \(fast restart",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts2, self.prefix)

    def run(self) -> None:
        self._configure_storage()
        self._configure_traffic_servers()
        self._populate_cache_and_kill_traffic_server()
        self._restart_after_unclean_shutdown()
        self._verify_restart_logs()
        self._clear_shared_memory()


def test_cache_shm_unclean_shutdown(ats_factory: ATSFactory, curl: Curl) -> None:
    CacheShmUncleanShutdownScenario(ats_factory, curl).run()
