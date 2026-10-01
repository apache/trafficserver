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


def test_cache_shm_dir_invalid(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Out-of-range shm directory fields fall back to disk recovery.

    The test separately corrupts ``write_pos`` and ``freelist[0]`` in a clean
    stripe segment. Each restart may attach the segment itself, but must reject
    the unsafe directory contents before they can drive out-of-bounds disk I/O.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    def _assert_rejected_directory(ats: ATS) -> None:
        """assert rejected directory.

        :param ats: Traffic Server instance configured or queried by this step.
        """
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

    prefix = shm_prefix("d")
    stripe_file = Path("/dev/shm") / f"{prefix.lstrip('/')}s0"
    poke_script = Path(__file__).parent / "shm_poke.py"
    if platform.system() != "Linux":
        pytest.skip("shm byte-poke gates need Linux /dev/shm")
    disk = make_disk(ats_factory.run_directory, "disk.img")
    origin = services.verifier_server("shmd-origin", REPLAY)
    ts1 = configure_shm_ats(
        ats_factory,
        "shmd_ts1",
        prefix,
        [disk],
        origin_port=origin.http_port,
    )
    ts2 = configure_shm_ats(
        ats_factory,
        "shmd_ts2",
        prefix,
        [disk],
        origin_port=origin.http_port,
    )
    ts3 = configure_shm_ats(
        ats_factory,
        "shmd_ts3",
        prefix,
        [disk],
        origin_port=origin.http_port,
    )
    origin.start()
    ts1.start()
    result = services.verifier_client(
        "shmd-fill-client",
        REPLAY,
        http_ports=[ts1.http_port],
        keys="fill",
        other_args="--thread-limit 1",
    ).run()

    assert result.returncode == 0, result.output
    clean_shutdown(ts1)
    result = ts1.run(sys.executable, poke_script, stripe_file, "16", "ffffffffffff0000")

    assert result.returncode == 0, result.output
    ts2.start()
    result = services.verifier_client(
        "shmd-write-pos-client",
        REPLAY,
        http_ports=[ts2.http_port],
        keys="hit_write_pos",
        other_args="--thread-limit 1",
    ).run()

    assert result.returncode == 0, result.output
    clean_shutdown(ts2)
    result = ts2.run(sys.executable, poke_script, stripe_file, "72", "ffff")

    assert result.returncode == 0, result.output
    ts3.start()
    result = services.verifier_client(
        "shmd-freelist-client",
        REPLAY,
        http_ports=[ts3.http_port],
        keys="hit_freelist",
        other_args="--thread-limit 1",
    ).run()

    assert result.returncode == 0, result.output
    clean_shutdown(ts3)
    assert_log(
        ts1,
        contains=(
            r"cache shm: creating fresh control segment",
            r"cache shm: created stripe \S+ \(\d+ bytes\) for key=",
            r"cache shm: marking clean shutdown",
        ),
        excludes=(r"shm directory invalid for",),
    )
    _assert_rejected_directory(ts2)
    _assert_rejected_directory(ts3)
    clear_shm(ts3, prefix)
