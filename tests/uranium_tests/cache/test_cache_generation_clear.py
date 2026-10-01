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
import time
import uuid

from tools.uranium.services import ATS, Curl, assert_matches_gold

GOLD_DIRECTORY = Path(__file__).parent / "gold"
XDEBUG_HEADER = "x-cache,x-cache-key,via,x-cache-generation"


def configure_traffic_server(*, _ats: ATS) -> None:
    """Cache generation clear configure traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.records.update({
        "proxy.config.body_factory.enable_customizations": 3,
        "proxy.config.http.cache.generation": -1,
    })
    _ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key,via,x-cache-generation")
    _ats.remap_config.add_line("map /default/ http://127.0.0.1/ @plugin=generator.so")


def start_traffic_server(*, _ats: ATS) -> None:
    """Cache generation clear start traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.start()


def request_object(gold_name: str, *, _ats: ATS, _curl: Curl, object_id: uuid.UUID) -> None:
    """Cache generation clear request object.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param object_id: Test-local object id configured by the test.
    :param gold_name: Gold name used by this test step.
    """
    result = _curl.run_for(
        _ats,
        (
            f"--verbose --output /dev/null --header 'x-debug: {XDEBUG_HEADER}' "
            f"'http://127.0.0.1:{_ats.http_port}/default/cache/10/{object_id}'"),
    )
    assert result.returncode == 0, result.output
    assert_matches_gold(result.output, GOLD_DIRECTORY / gold_name)


def verify_initial_generation(*, _ats: ATS, _curl: Curl, object_id: uuid.UUID) -> None:
    """Cache generation clear verify initial generation.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param object_id: Test-local object id configured by the test.
    """
    request_object("miss_default-1.gold", _ats=_ats, _curl=_curl, object_id=object_id)
    request_object("hit_default-1.gold", _ats=_ats, _curl=_curl, object_id=object_id)


def clear_cache(*, _ats: ATS) -> None:
    """Cache generation clear clear cache.

    :param _ats: Test-local ats configured by the test.
    """
    result = _ats.traffic_ctl("cache", "clear")

    assert result.returncode == 0, result.output
    time.sleep(15)


def verify_new_generation(*, _ats: ATS, _curl: Curl, object_id: uuid.UUID) -> None:
    """Cache generation clear verify new generation.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param object_id: Test-local object id configured by the test.
    """
    request_object("miss_default0.gold", _ats=_ats, _curl=_curl, object_id=object_id)
    request_object("hit_default0.gold", _ats=_ats, _curl=_curl, object_id=object_id)
    request_object("hit_default0.gold", _ats=_ats, _curl=_curl, object_id=object_id)


def test_cache_generation_clear(ats: ATS, curl: Curl) -> None:
    """traffic_ctl cache clear advances the cache generation.

    :param ats: Traffic Server instance configured or queried by this step.
    :param curl: Transport-aware curl command runner.
    """
    object_id = uuid.uuid4()
    configure_traffic_server(_ats=ats)
    start_traffic_server(_ats=ats)
    verify_initial_generation(_ats=ats, _curl=curl, object_id=object_id)
    clear_cache(_ats=ats)
    verify_new_generation(_ats=ats, _curl=curl, object_id=object_id)
