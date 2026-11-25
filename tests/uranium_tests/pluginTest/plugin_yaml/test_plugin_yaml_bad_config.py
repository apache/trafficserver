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
    "path_null": "plugins:\n  - path:\n",
    # xdebug.so starts with no arguments, so a dropped params value would not stop it.
    "params_scalar": "plugins:\n  - path: xdebug.so\n    params: --enable=x-cache\n",
    "params_mapping": "plugins:\n  - path: xdebug.so\n    params:\n      enable: x-cache\n",
    "params_empty_item": "plugins:\n  - path: xdebug.so\n    params:\n      - --enable=x-cache\n      -\n",
    # A later document that is not empty or cannot be parsed is an error, so nothing in it is dropped unseen.
    "null_first_document": "---\n---\nplugins:\n  - path: xdebug.so\n",
    "malformed_second_document": "plugins:\n  - path: xdebug.so\n---\nplugins: [unclosed\n",
    "stray_token_after_document": "plugins:\n  - path: xdebug.so\n--- ,\n",
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


def test_plugin_yaml_directory_stops_startup(ats_factory: ATSFactory) -> None:
    """A directory in place of plugin.yaml stops startup with a diagnostic.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    # Writing a file beneath plugin.yaml makes plugin.yaml a directory.
    ats.write_config_file("plugin.yaml/placeholder", "")
    ats.expect_start_failure(r"EMERGENCY: plugin\.yaml: not a regular file")
    ats.start()


# Each document names no plugins, so it loads none, the same as plugins: [].
EMPTY_PLUGIN_YAML = {
    "empty_file": "",
    "comment_only": "# no plugins yet\n",
    "only_empty_documents": "---\n---\n",
    "entries_commented_out": "plugins:\n#  - path: libfoo.so\n",
}


@pytest.mark.parametrize("content", EMPTY_PLUGIN_YAML.values(), ids=EMPTY_PLUGIN_YAML.keys())
def test_plugin_yaml_naming_no_plugins_starts(ats_factory: ATSFactory, content: str) -> None:
    """A plugin.yaml that names no plugins does not stop startup.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param content: The plugin.yaml document under test.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.write_config_file("plugin.yaml", content)
    ats.start()
    assert ats.is_running
