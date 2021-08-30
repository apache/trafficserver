#!/usr/bin/env python3
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Compares two records.config files, printing which items are different and
# what the before/after values are.
# Ignores FLOAT differences and @foo@ values from the source code defaults.
import sys


def parse_records_file(filename):
    fh = open(filename)
    settings = {}
    for line in fh:
        line = line.strip()
        if line.startswith('CONFIG') or line.startswith('LOCAL'):
            parts = line.split()
            if parts[2] == 'FLOAT':
                parts[3] = parts[3].rstrip('0')
            if parts[2] == 'INT' and parts[3][-1] in 'KMG':
                unit = parts[3][-1]
                val = parts[3][:-1]
                if unit == 'K':
                    val = int(val) * 1024
                if unit == 'M':
                    val = int(val) * 1048576
                if unit == 'G':
                    val = int(val) * 1073741824
                parts[3] = str(val)
            try:
                settings[parts[1]] = parts[3]
            except IndexError:
                sys.stderr.write(f"Skipping malformed line: {line}\n")
                continue
    return settings


def compare_settings(old, new):
    for key in sorted(tuple(set(old) | set(new))):
        if key not in old:
            old[key] = "missing"
        if key not in new:
            new[key] = "missing"

    for key in sorted(old):
        if old[key].startswith('@') and old[key].endswith('@'):
            # Skip predefined values
            continue
        if old[key] != new[key]:
            print(f"{key} {old[key]} -> {new[key]}")


if __name__ == '__main__':
    settings_orig = parse_records_file(sys.argv[1])
    settings_new = parse_records_file(sys.argv[2])
    compare_settings(settings_orig, settings_new)
