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


class CacheShmConcurrentAttachScenario:
    """A second live writer refuses the first process's shm directory.

    The instances deliberately share a shm prefix but use independent disk
    spans. This isolates the live-owner guard from ordinary disk contention and
    verifies that the refused instance continues with shm disabled.
    """

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.prefix = shm_prefix("c")
        self.path = f"/cache/40/{uuid.uuid4()}"

    def _configure_storage(self) -> None:
        self.disk_a = make_disk(self.ats_factory.run_directory, "disk_a.img")
        self.disk_b = make_disk(self.ats_factory.run_directory, "disk_b.img")

    def _configure_traffic_servers(self) -> None:
        self.ts1 = configure_shm_ats(self.ats_factory, "shmc_ts1", self.prefix, [self.disk_a])
        self.ts2 = configure_shm_ats(self.ats_factory, "shmc_ts2", self.prefix, [self.disk_b])

    def _run_concurrent_instances(self) -> None:
        self.ts1.start()
        get_200(self.curl, self.ts1, self.path)
        self.ts2.start()
        get_200(self.curl, self.ts2, self.path)

        assert self.ts1.is_running
        assert self.ts2.is_running

    def _cleanly_shutdown_instances(self) -> None:
        clean_shutdown(self.ts2)
        clean_shutdown(self.ts1)

    def _verify_live_owner_was_refused(self) -> None:
        assert_log(self.ts1, contains=(r"cache shm: creating fresh control segment",))
        assert_log(
            self.ts2,
            contains=(r"disabling shm this run to avoid concurrent attach",),
            excludes=(
                r"cache shm: creating fresh control segment",
                r"cache shm: attaching up to \d+ stripes \(fast restart",
            ),
        )

    def _clear_shared_memory(self) -> None:
        clear_shm(self.ts1, self.prefix)

    def run(self) -> None:
        self._configure_storage()
        self._configure_traffic_servers()
        self._run_concurrent_instances()
        self._cleanly_shutdown_instances()
        self._verify_live_owner_was_refused()
        self._clear_shared_memory()


def test_cache_shm_concurrent_attach(ats_factory: ATSFactory, curl: Curl) -> None:
    CacheShmConcurrentAttachScenario(ats_factory, curl).run()
