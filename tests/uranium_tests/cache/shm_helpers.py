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
"""Shared setup for native cache shared-memory tests."""

from collections.abc import Iterable, Sequence
from pathlib import Path
import os
import re

from tools.uranium.services import ATS, ATSFactory, Curl

DISK_SIZE = 256 * 1024 * 1024


def make_disk(root: Path, name: str, size: int = DISK_SIZE) -> Path:
    """Create a sparse cache span in the test sandbox."""

    directory = root / "shared-storage"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    with path.open("ab") as disk:
        disk.truncate(size)
    path.chmod(0o666)
    return path


def shm_prefix(tag: str) -> str:
    """Return a short per-process POSIX shared-memory prefix."""

    return f"/cshm{tag}-{os.getpid() % 100000}-"


def configure_shm_ats(
    factory: ATSFactory,
    name: str,
    prefix: str,
    storage_paths: Sequence[Path],
    *,
    origin_port: int | None = None,
    enabled: bool = True,
    purge: bool = False,
    debug_tags: str = "cache_shm",
) -> ATS:
    """Create one ATS instance using explicit cache spans and shm settings."""

    ats = factory.create(name)
    storage_lines = ["cache:", "  spans:"]
    for index, storage_path in enumerate(storage_paths):
        storage_lines.extend([
            f"    - name: disk.{index}",
            f"      path: {storage_path}",
            f"      size: {DISK_SIZE}",
        ])
    storage_lines.extend([
        "  volumes:",
        "    - id: 1",
        "      scheme: http",
        "      size: 100%",
    ])
    ats.storage_config.add_lines(storage_lines)
    ats.records.update(
        {
            "proxy.config.cache.shm.enabled": int(enabled),
            "proxy.config.cache.shm.name_prefix": prefix,
            "proxy.config.cache.shm.use_hugepages": 0,
            "proxy.config.cache.shm.purge_stale_on_start": int(purge),
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": debug_tags,
            "proxy.config.diags.output.diag": "L",
            "proxy.config.http.wait_for_cache": 1,
        })
    ats.plugin_config.add_line("xdebug.so --enable=x-cache,via")
    target = f"http://127.0.0.1:{origin_port}/" if origin_port is not None else "http://127.0.0.1/ @plugin=generator.so"
    ats.remap_config.add_line(f"map / {target}")
    return ats


def assert_log(ats: ATS, *, contains: Iterable[str] = (), excludes: Iterable[str] = ()) -> str:
    """Check regular expressions against one ATS diags.log."""

    content = ats.diags_log.read_text(errors="replace")
    for expression in contains:
        assert re.search(expression, content, re.MULTILINE), f"{ats.name} diags.log should contain {expression!r}\n{content}"
    for expression in excludes:
        assert re.search(expression, content, re.MULTILINE) is None, \
            f"{ats.name} diags.log should exclude {expression!r}\n{content}"
    return content


def get_200(curl: Curl, ats: ATS, path: str) -> None:
    """Issue one generator request and require an HTTP 200 response."""

    result = curl.run_for(
        ats,
        (
            f"--silent --output /dev/null --write-out '%{{http_code}}\n' --header 'x-debug: x-cache,via' "
            f"'http://127.0.0.1:{ats.http_port}{path}'"),
    )
    assert result.returncode == 0, result.output
    assert result.stdout.strip() == "200", result.output


def clean_shutdown(ats: ATS) -> None:
    """Drain and cleanly terminate one ATS instance."""

    ats.drain_and_stop()


def clear_shm(ats: ATS, *prefixes: str) -> None:
    """Remove shared-memory segments left for fast restart."""

    for prefix in prefixes:
        result = ats.traffic_ctl("cache", "shm", "clear", "--prefix", prefix)
        assert result.returncode == 0, result.output
        assert "Invalid argument" not in result.stderr
