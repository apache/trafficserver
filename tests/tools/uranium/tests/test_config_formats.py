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
"""Compatibility of structured replay metadata with ATS 10.2 files."""

from tools.uranium.config_formats import multicert_text, storage_text


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
