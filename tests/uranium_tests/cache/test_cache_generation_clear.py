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


class CacheGenerationClearScenario:
    """traffic_ctl cache clear advances the cache generation."""

    def __init__(self, ats: ATS, curl: Curl) -> None:
        self.ats = ats
        self.curl = curl
        self.object_id = uuid.uuid4()

    def _configure_traffic_server(self) -> None:
        self.ats.records.update({
            "proxy.config.body_factory.enable_customizations": 3,
            "proxy.config.http.cache.generation": -1,
        })
        self.ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key,via,x-cache-generation")
        self.ats.remap_config.add_line("map /default/ http://127.0.0.1/ @plugin=generator.so")

    def _start_traffic_server(self) -> None:
        self.ats.start()

    def _request_object(self, gold_name: str) -> None:
        result = self.curl.run_for(
            self.ats,
            "--verbose",
            "--output",
            "/dev/null",
            "--header",
            f"x-debug: {XDEBUG_HEADER}",
            f"http://127.0.0.1:{self.ats.http_port}/default/cache/10/{self.object_id}",
        )
        assert result.returncode == 0, result.output
        assert_matches_gold(result.output, GOLD_DIRECTORY / gold_name)

    def _verify_initial_generation(self) -> None:
        self._request_object("miss_default-1.gold")
        self._request_object("hit_default-1.gold")

    def _clear_cache(self) -> None:
        result = self.ats.traffic_ctl("cache", "clear")

        assert result.returncode == 0, result.output
        time.sleep(15)

    def _verify_new_generation(self) -> None:
        self._request_object("miss_default0.gold")
        self._request_object("hit_default0.gold")
        self._request_object("hit_default0.gold")

    def run(self) -> None:
        self._configure_traffic_server()
        self._start_traffic_server()
        self._verify_initial_generation()
        self._clear_cache()
        self._verify_new_generation()


def test_cache_generation_clear(ats: ATS, curl: Curl) -> None:
    CacheGenerationClearScenario(ats, curl).run()
