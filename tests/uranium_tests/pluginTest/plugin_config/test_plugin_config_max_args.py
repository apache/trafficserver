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

from tools.uranium.services import ATS, Curl

# plugin.config accepts up to 64 tokens per line, the plugin path included.
MAX_PLUGIN_ARGS = 64


def test_plugin_config_line_with_max_args(ats: ATS, curl: Curl) -> None:
    """A plugin.config line with the maximum number of tokens passes every one to the plugin.

    The filler tokens make the line exactly that long, which puts the last token in the final slot
    of the argument vector. It is the only token that enables an xdebug feature, so the X-Cache
    header in the response shows that it reached the plugin, and nothing may be reported dropped.

    :param ats: Traffic Server instance configured or queried by this step.
    :param curl: Transport-aware curl command runner.
    """
    filler = ["filler"] * (MAX_PLUGIN_ARGS - 2)
    ats.plugin_config.add_line(" ".join(["xdebug.so", *filler, "--enable=x-cache"]))
    ats.remap_config.add_line("map / http://127.0.0.1/ @plugin=generator.so")
    ats.start()

    url = f"http://127.0.0.1:{ats.http_port}/cache/10/plugin-config-max-args"
    result = curl.run_for(ats, f"--verbose --output /dev/null --header 'X-Debug: x-cache' '{url}'")
    assert result.returncode == 0, result.output
    assert "< X-Cache: miss" in result.output, result.output
    assert "Exceeded max number of args" not in ats.diags_log.read_text(errors="replace")
