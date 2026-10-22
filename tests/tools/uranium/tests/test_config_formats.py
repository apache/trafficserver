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
"""Compatibility of structured replay metadata with ATS 9.2 files."""

from tools.uranium.config_formats import merge_records, multicert_text, records_text, storage_text


def test_records_allow_scalar_names_with_child_records() -> None:
    """ATS 9.2 autoconfig and autoconfig.scale are independent records."""
    values = merge_records(
        {"exec_thread": {
            "autoconfig": 1
        }}, {
            "proxy.config.exec_thread.autoconfig": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.5,
        })
    assert records_text(values) == (
        "CONFIG proxy.config.exec_thread.autoconfig INT 0\n"
        "CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.5\n")


def test_records_preserve_manager_node_prefix() -> None:
    """Manager node settings must not acquire a proxy.config prefix."""
    assert records_text(merge_records({}, {"proxy.node.config.manager_log_filename": "manager.log"
                                          })) == ("CONFIG proxy.node.config.manager_log_filename STRING manager.log\n")


def test_records_preserve_types_and_nested_names() -> None:
    """Classic records preserve integer, float, null, and string values."""
    assert records_text({
        "http": {
            "cache": {
                "http": True
            }
        },
        "ratio": 1.5,
        "name": "a b",
        "unset": None
    }) == (
        "CONFIG proxy.config.http.cache.http INT 1\n"
        "CONFIG proxy.config.ratio FLOAT 1.5\n"
        "CONFIG proxy.config.name STRING a b\n"
        "CONFIG proxy.config.unset STRING NULL\n")


def test_multicert_structured_and_line_forms_agree() -> None:
    """Native and replay tests must configure the same TLS entries."""
    expected = 'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key\n'
    assert multicert_text(
        {"ssl_multicert": [{
            "dest_ip": "*",
            "ssl_cert_name": "server.pem",
            "ssl_key_name": "server.key",
        }]}) == expected
    assert multicert_text(
        [
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ]) == expected
    assert multicert_text([expected.rstrip()]) == expected


def test_storage_metadata_preserves_volume_options() -> None:
    """Keep cache allocation and per-volume behavior on the old file format."""
    storage, volumes = storage_text(
        {
            "cache":
                {
                    "spans": [{
                        "path": "storage",
                        "size": "256M",
                        "volume": 1
                    }],
                    "volumes": [{
                        "id": 1,
                        "scheme": "http",
                        "size": "128M",
                        "ram_cache": False
                    }],
                }
        })
    assert storage == "storage 256M volume=1\n"
    assert volumes == "volume=1 scheme=http size=128 ramcache=false\n"
