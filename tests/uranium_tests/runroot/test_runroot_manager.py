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
"""Check that traffic_manager propagates an external runroot descriptor."""
import os
import pwd
import subprocess
import time
from tools.uranium.services import ProceduralContext, ServiceFactory


def test_runroot_manager(procedural_context: ProceduralContext, services: ServiceFactory) -> None:
    """Start the manager with a relocated descriptor and inspect its child.

    :param procedural_context: Isolated directory and installed binaries.
    :param services: Manager process owner.
    """
    root = procedural_context.run_directory / "runroot"
    descriptor = procedural_context.run_directory / "rr_tmp"
    descriptor.mkdir()
    binary = procedural_context.runtime.ats_bin
    result = subprocess.run(
        [binary / "traffic_layout", "init", "--path", root, "--absolute"], capture_output=True, text=True, check=False, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    (root / "runroot.yaml").rename(descriptor / "runroot.yaml")
    assert (descriptor / "runroot.yaml").is_file()
    if os.geteuid() == 0:
        account = pwd.getpwnam("nobody")
        for path in (root / "var" / "trafficserver").rglob("*"):
            os.chown(path, account.pw_uid, account.pw_gid)
        os.chown(root / "var" / "trafficserver", account.pw_uid, account.pw_gid)
    manager = services.process("manager", [binary / "traffic_manager", f"--run-root={descriptor}"])
    manager.start()
    expected = f"traffic_server: using root directory '{root}'"
    deadline = time.monotonic() + 10
    while expected not in manager.output and manager.is_running and time.monotonic() < deadline:
        time.sleep(0.1)
    # The child's startup printf is block-buffered when inherited stdout is a pipe.
    manager.stop()
    assert expected in manager.output, manager.output
