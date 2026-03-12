#!/usr/bin/env python3
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""
Generate test MMDB files for header_rewrite geo lookup unit tests.

Two schemas exist in the wild:

  Nested (GeoLite2/GeoIP2/DBIP):  country -> iso_code
  Flat   (vendor-specific):       country_code (top-level)

This script generates one MMDB file for each schema so the C++ test
can verify that auto-detection works for both.

Requires: pip install mmdb-writer
"""

import os
import sys

try:
    from mmdb_writer import MMDBWriter, MmdbU32
    import netaddr
except ImportError:
    print("SKIP: mmdb-writer or netaddr not installed (pip install mmdb-writer)", file=sys.stderr)
    sys.exit(0)


def net(cidr):
    return netaddr.IPSet([netaddr.IPNetwork(cidr)])


def generate_flat(path):
    """Flat schema: country_code at top level (vendor databases)."""
    w = MMDBWriter(ip_version=4, database_type="Test-Flat-GeoIP")
    w.insert_network(
        net("8.8.8.0/24"), {
            "country_code": "US",
            "autonomous_system_number": MmdbU32(15169),
            "autonomous_system_organization": "GOOGLE",
        })
    w.insert_network(
        net("1.2.3.0/24"), {
            "country_code": "KR",
            "autonomous_system_number": MmdbU32(9286),
            "autonomous_system_organization": "KINX",
        })
    w.to_db_file(path)


def generate_nested(path):
    """Nested schema: country/iso_code (GeoLite2, GeoIP2, DBIP)."""
    w = MMDBWriter(ip_version=4, database_type="Test-Nested-GeoIP2")
    w.insert_network(
        net("8.8.8.0/24"), {
            "country": {
                "iso_code": "US",
                "names": {
                    "en": "United States"
                }
            },
            "autonomous_system_number": MmdbU32(15169),
            "autonomous_system_organization": "GOOGLE",
        })
    w.insert_network(
        net("1.2.3.0/24"), {
            "country": {
                "iso_code": "KR",
                "names": {
                    "en": "South Korea"
                }
            },
            "autonomous_system_number": MmdbU32(9286),
            "autonomous_system_organization": "KINX",
        })
    w.to_db_file(path)


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."

    flat_path = os.path.join(outdir, "test_flat_geo.mmdb")
    nested_path = os.path.join(outdir, "test_nested_geo.mmdb")

    generate_flat(flat_path)
    generate_nested(nested_path)

    print(f"Generated {flat_path} ({os.path.getsize(flat_path)} bytes)")
    print(f"Generated {nested_path} ({os.path.getsize(nested_path)} bytes)")
