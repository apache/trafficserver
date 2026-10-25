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


def test_cache_shm_fast_restart(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A clean restart attaches the cache directory from shared memory.

    The first instance fills the shared disk and marks shm clean during
    shutdown. The second uses the same disk and prefix; its replay contains a
    sentinel origin response so only a cache hit can pass.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    prefix = shm_prefix("")
    disk = make_disk(ats_factory.run_directory, "disk.img")
    origin = services.verifier_server("shm-origin", REPLAY)
    ts1 = configure_shm_ats(
        ats_factory,
        "shm_ts1",
        prefix,
        [disk],
        origin_port=origin.http_port,
    )
    ts2 = configure_shm_ats(
        ats_factory,
        "shm_ts2",
        prefix,
        [disk],
        origin_port=origin.http_port,
    )
    origin.start()
    ts1.start()
    result = services.verifier_client(
        "shm-fill-client",
        REPLAY,
        http_ports=[ts1.http_port],
        keys="fill",
        other_args="--thread-limit 1",
    ).run()

    assert result.returncode == 0, result.output
    clean_shutdown(ts1)
    ts2.start()
    result = services.verifier_client(
        "shm-hit-client",
        REPLAY,
        http_ports=[ts2.http_port],
        keys="hit",
        other_args="--thread-limit 1",
    ).run()

    assert result.returncode == 0, result.output
    clean_shutdown(ts2)
    assert_log(
        ts1,
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
        ts2,
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
    clear_shm(ts2, prefix)
