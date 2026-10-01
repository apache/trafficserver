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

import time
from pathlib import Path

from tools.uranium.services import ATS


def configure_traffic_server(*, _ats: ATS) -> None:
    """Cache config reload configure traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "rpc|config",
    })
    _ats.cache_config.add_line("dest_domain=example.com ttl-in-cache=30d")


def start_traffic_server(*, _ats: ATS) -> None:
    """Cache config reload start traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.start()


def reload_configuration(config_file: Path, token: str, *, _ats: ATS) -> None:
    """Cache config reload reload configuration.

    :param _ats: Test-local ats configured by the test.
    :param config_file: Path to the config file.
    :param token: Token used by this test step.
    """
    config_file.touch()
    time.sleep(2)
    result = _ats.traffic_ctl("config", "reload", "-m", "-t", token, "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode in (0, 2), result.output
    time.sleep(3)


def reload_cache_configuration(*, _ats: ATS) -> None:
    """Cache config reload reload cache configuration.

    :param _ats: Test-local ats configured by the test.
    """
    reload_configuration(_ats.cache_config.path, "reload_cache_test", _ats=_ats)


def reload_hosting_configuration(*, _ats: ATS) -> None:
    """Cache config reload reload hosting configuration.

    :param _ats: Test-local ats configured by the test.
    """
    reload_configuration(_ats.hosting_config.path, "reload_hosting_test", _ats=_ats)


def test_cache_config_reload(ats: ATS) -> None:
    """Reload cache.config and hosting.config after each file changes.

    :param ats: Traffic Server instance configured or queried by this step.
    """
    configure_traffic_server(_ats=ats)
    start_traffic_server(_ats=ats)
    reload_cache_configuration(_ats=ats)
    reload_hosting_configuration(_ats=ats)
