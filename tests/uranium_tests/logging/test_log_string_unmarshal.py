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
"""A plugin string log field decoded by TSLogStringUnmarshal leaves the fields after it intact."""

from tools.uranium.services import ATSFactory, Curl, wait_for_file_lines

ALPHABET = "abcdefghijklmnopqrstuvwxyz"
# The marshalled string, with its NUL, is padded to a multiple of 8 bytes, so lengths 0, 8 and 16 are the ones
# whose NUL starts a new 8-byte block.
LENGTHS = (0, 1, 7, 8, 9, 15, 16, 17)


def test_log_string_unmarshal(ats_factory: ATSFactory, curl: Curl) -> None:
    """Every field after a plugin string field reads back as sent, whatever the string's length.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    ats = ats_factory.create("ts")
    ats.records.update({"proxy.config.log.max_secs_per_buffer": 1})
    ats.copy_custom_plugin("{AtsTestPluginsDir}/log_string_field.so")
    ats.plugin_config.add_line("log_string_field.so")
    ats.remap_config.add_line("map / http://127.0.0.1:1/ @action=deny")
    ats.set_logging_yaml(
        {
            "logging": {
                "formats": [{"name": "tstr", "format": "v=[%<tstr>] p=%<cqup> s=%<pssc> m=%<cqhm> end"}],
                "logs": [{"filename": "tstr", "format": "tstr"}],
            }
        }
    )
    ats.start()
    cases = []
    for n in LENGTHS:
        value = ALPHABET[:n]
        cases.append((f"/len-{n}", value, {"X-Field": value or "EMPTY"}))
    cases.append(("/dash", "-", {"X-Field": "-"}))
    cases.append(("/absent", "-", None))
    for path, _, headers in cases:
        result = curl.get(ats, path, headers=headers)
        assert result.returncode == 0, result.output
    log = ats.log_directory / "tstr.log"
    content = wait_for_file_lines(log, r"^v=", len(cases))
    lines = [line for line in content.splitlines() if line.strip()]
    expected = [f"v=[{value}] p={path[1:]} s=403 m=GET end" for path, value, _ in cases]
    assert lines == expected, "\n".join(f"want {w!r}\n got {g!r}" for w, g in zip(expected, lines))
