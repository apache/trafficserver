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


import pytest

from tools.uranium.services import ATSFactory

# Each document is a plugin.yaml that cannot be loaded as written.
BAD_PLUGIN_YAML = {
    "syntax_error": "plugins:\n  - path: [unclosed\n",
    "missing_path": "plugins:\n  - enabled: true\n",
    "scalar_entry": "plugins:\n  - libfoo.so\n",
    "wrong_type": "plugins:\n  - path: libfoo.so\n    enabled: maybe\n",
    "not_a_map": "just a string\n",
}


@pytest.mark.parametrize("content", BAD_PLUGIN_YAML.values(), ids=BAD_PLUGIN_YAML.keys())
def test_plugin_yaml_bad_config_stops_startup(ats_factory: ATSFactory, content: str) -> None:
    """A plugin.yaml that cannot be loaded stops startup instead of running without its plugins.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param content: The plugin.yaml document under test.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.write_config_file("plugin.yaml", content)
    ats.expect_start_failure(r"EMERGENCY: plugin\.yaml")
    ats.start()
