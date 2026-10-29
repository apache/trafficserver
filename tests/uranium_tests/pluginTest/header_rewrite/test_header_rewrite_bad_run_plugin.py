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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

BAD_RUN_PLUGIN_NESTED_BAD_RULE = """\
cond %{REMAP_PSEUDO_HOOK}
  if
    cond %{TRUE}
      run-plugin conf_remap.so no_such_conf_remap_file.yaml
  endif
"""
BAD_RUN_PLUGIN_BAD_RULE = """\
cond %{REMAP_PSEUDO_HOOK}
  run-plugin conf_remap.so no_such_conf_remap_file.yaml
"""

BAD_RUN_PLUGIN_ERROR_MARKER = "run-plugin unable to load"


def require_plugins(*, _ats: ATS) -> None:
    """Skip when either installed plugin needed by the scenario is absent.

    :param _ats: Test-local ats configured by the test.
    """

    if not _ats.plugin_exists("header_rewrite.so") or not _ats.plugin_exists("conf_remap.so"):
        pytest.skip("header_rewrite.so and conf_remap.so are required")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin used before and after the rejected reload.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: reload.example.com\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_startup_ats(*, _ats_factory: ATSFactory) -> ATS:
    """Configure an invalid top-level run-plugin rule.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = _ats_factory.create("ts-startup", enable_cache=False)
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "header_rewrite",
    })
    ats.write_config_file("bad_run_plugin.conf", BAD_RUN_PLUGIN_BAD_RULE)
    ats.remap_config.add_line(
        "map http://startup.example.com/ http://127.0.0.1/ "
        "@plugin=header_rewrite.so @pparam=bad_run_plugin.conf")
    ats.expect_start_failure(BAD_RUN_PLUGIN_ERROR_MARKER)
    return ats


def configure_reload_ats(*, _ats_factory: ATSFactory, _origin: OriginServer) -> ATS:
    """Configure the valid remap generation used by the live server.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    ats = _ats_factory.create("ts-reload", enable_cache=False)
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "header_rewrite",
    })
    ats.write_config_file("nested_bad_run_plugin.conf", BAD_RUN_PLUGIN_NESTED_BAD_RULE)
    ats.remap_config.add_line(f"map http://reload.example.com http://127.0.0.1:{_origin.port}")
    return ats


def verify_startup_rejection(*, _ats_factory: ATSFactory) -> None:
    """Confirm invalid startup configuration exits cleanly rather than aborting.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = configure_startup_ats(_ats_factory=_ats_factory)
    ats.start()
    assert BAD_RUN_PLUGIN_ERROR_MARKER in ats.diags_log.read_text(errors="replace")
    assert "Traffic Server is fully initialized" not in ats.traffic_out.read_text(errors="replace")


def request(*, _ats: ATS, _curl: Curl) -> None:
    """Verify the currently active remap generation still serves traffic.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.get(_ats, headers={"Host": "reload.example.com"}, options=f"--verbose")
    assert result.returncode == 0, result.output
    assert "200 OK" in result.stderr


def install_invalid_remap(*, _ats: ATS, _origin: OriginServer) -> None:
    """Replace remap.config with a nested run-plugin whose instance cannot load.

    :param _ats: Test-local ats configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    _ats.remap_config.path.write_text(
        f"map http://reload.example.com http://127.0.0.1:{_origin.port} "
        "@plugin=header_rewrite.so @pparam=nested_bad_run_plugin.conf\n")


def reject_reload(*, _ats: ATS) -> None:
    """Reload the invalid table and wait until ATS reports the failure.

    :param _ats: Test-local ats configured by the test.
    """

    token = "bad-run-plugin"
    result = _ats.traffic_ctl("config", "reload", "--token", token)
    assert result.returncode == 0, result.output
    deadline = time.monotonic() + 15
    latest = ""
    while time.monotonic() < deadline:
        status = _ats.traffic_ctl("config", "status", "--token", token)
        latest = status.output.lower()
        if "failed" in latest:
            return
        if "success" in latest:
            break
        time.sleep(0.1)
    raise AssertionError(f"Invalid remap reload was not rejected:\n{latest}")


def test_header_rewrite_bad_run_plugin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Failed run-plugin initialization is rejected without losing the prior remap generation.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_reload_ats(_ats_factory=ats_factory, _origin=_origin)

    require_plugins(_ats=_ats)
    verify_startup_rejection(_ats_factory=ats_factory)
    _origin.start()
    _ats.start()
    request(_ats=_ats, _curl=curl)
    install_invalid_remap(_ats=_ats, _origin=_origin)
    reject_reload(_ats=_ats)
    request(_ats=_ats, _curl=curl)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if BAD_RUN_PLUGIN_ERROR_MARKER in _ats.diags_log.read_text(errors="replace"):
            return
        time.sleep(0.1)
    raise AssertionError("The rejected reload did not log the run-plugin initialization failure")
