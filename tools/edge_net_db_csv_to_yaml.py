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
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
Convert a CSV file with IP ranges to an abuse_shield trusted IPs YAML file.

Input CSV format (first field is IP range):
    3.0.4.148-3.0.4.148,yahoo,yahoo,aps1,prod;aws
    3.0.27.18-3.0.27.18,partner,AWS_TWEC,aps1,aws

Output YAML format:
    trusted_ips:
      - 3.0.4.148
      - 3.0.27.18
"""

import argparse
import csv
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    """
    Parse command line arguments.
    :return: The parsed arguments.
    """
    parser = argparse.ArgumentParser(description="Convert CSV with IP ranges to abuse_shield trusted IPs YAML")
    parser.add_argument("input_file", help="Path to input CSV file", type=Path)
    parser.add_argument("output_file", help="Path to output YAML file", type=Path)
    return parser.parse_args()


def parse_ip_range(ip_range: str) -> str:
    """
    Parse an IP range string and return the appropriate format.

    If start and end IPs are the same, return just the single IP.
    Otherwise, return the range as-is.
    :param ip_range: The IP range string to parse.
    :return: The parsed IP range string.
    """
    ip_range = ip_range.strip()
    if "-" in ip_range:
        parts = ip_range.split("-", 1)
        if len(parts) == 2:
            start_ip = parts[0].strip()
            end_ip = parts[1].strip()
            if start_ip == end_ip:
                return start_ip
            return f"{start_ip}-{end_ip}"
    return ip_range


def convert_csv_to_trusted_ips(input_path: Path, output_path: Path) -> int:
    """
    Read the CSV file and write the trusted IPs YAML file.

    :param input_path: The path to the input CSV file.
    :param output_path: The path to the output YAML file.
    :return: The number of IPs/ranges written.
    """
    ip_entries = []

    with open(input_path, "r", newline="") as f:
        reader = csv.reader(f)
        for line_num, row in enumerate(reader, 1):
            if not row:
                print(f"Warning: Skipping empty line {line_num}", file=sys.stderr)
                continue

            ip_range = row[0].strip()
            if not ip_range or ip_range.startswith("#"):
                continue

            entry = parse_ip_range(ip_range)
            ip_entries.append(entry)

    with open(output_path, "w") as f:
        f.write("trusted_ips:\n")
        for entry in ip_entries:
            f.write(f'  - "{entry}"\n')

    return len(ip_entries)


def main() -> int:
    """
    Main entry point.
    :return: The exit code.
    """
    args = parse_args()

    try:
        count = convert_csv_to_trusted_ips(args.input_file, args.output_file)
        print(f"Converted {count} IP entries to {args.output_file}")
        return 0
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
