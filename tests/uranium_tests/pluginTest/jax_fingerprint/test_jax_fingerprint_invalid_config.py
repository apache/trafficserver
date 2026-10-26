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

import re

import pytest

from tools.uranium.services import ATSFactory, wait_for_file_lines

CASES = [
    (
        'Reject unknown configuration keys',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
      via-header: x-jax-via
""",
        "Unknown key 'via-header' in fingerprint entry",
    ),
    (
        'Reject duplicate fingerprint settings',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: false
      standalone: true
""",
        "Duplicate key 'standalone' in fingerprint entry",
    ),
    (
        'Reject duplicate fingerprints lists',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
  fingerprints:
    - method: JA3
""",
        "Duplicate key 'fingerprints' in jax_fingerprint",
    ),
    (
        'Reject empty server names',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4
      servernames:
        - ""
""",
        "Each 'servernames' entry must be a non-empty server name",
    ),
    (
        'Reject servernames for request-based methods',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
      servernames:
        - abc.example
""",
        "'servernames' is not supported for JA4H",
    ),
    (
        'Reject a method listed twice for one registry',
        """
jax_fingerprint:
  fingerprints:
    - method: JA4
      standalone: true
      header: x-ja4-a
    - method: JA4
      standalone: true
      header: x-ja4-b
      export: jax_fingerprint
""",
        "method JA4 is listed more than once for registry 'jax_fingerprint'",
    ),
    (
        'Reject a log_field listed twice',
        """
jax_fingerprint:
  fingerprints:
    - method: JA3
      log_field: jaxfp
    - method: JA4
      log_field: jaxfp
""",
        "log_field 'jaxfp' is listed more than once",
    ),
]


@pytest.mark.parametrize("name,config,expected_error", CASES, ids=[case[0] for case in CASES])
def test_jax_fingerprint_invalid_config(ats_factory: ATSFactory, name: str, config: str, expected_error: str) -> None:
    """Reject ineffective configuration without crashing the proxy.

    :param ats_factory: Factory owning the proxy.
    :param name: Description of the invalid input.
    :param config: Invalid YAML content, including duplicate keys when relevant.
    :param expected_error: Required diagnostic regular expression.
    """
    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    ats.write_config_file("jax_fingerprint.yaml", config)
    ats.plugin_config.add_line("jax_fingerprint.so jax_fingerprint.yaml")
    ats.start()
    wait_for_file_lines(ats.diags_log, expected_error, 1)
    failed = r"Failed to load configuration from .*jax_fingerprint\.yaml"
    wait_for_file_lines(ats.diags_log, failed, 1)
    ats.stop()
    context_error = (
        "'jax_fingerprint' must be a map containing 'fingerprints'"
        if name == "Reject duplicate fingerprints lists" else "invalid fingerprint entry at line 4")
    context_error = re.escape(f"{ats.config_directory / 'jax_fingerprint.yaml'}: {context_error}")
    for line in ats.diags_log.read_text(errors="replace").splitlines():
        assert "FATAL:" not in line and "Unrecognized configuration value" not in line, line
        assert "ERROR:" not in line or re.search(f"{expected_error}|{failed}|{context_error}", line), line
