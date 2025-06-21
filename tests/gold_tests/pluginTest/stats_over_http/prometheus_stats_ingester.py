'''Parse ATS Prometheus stats with Prometheus to verify correct formatting.'''
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

import argparse
import sys
from urllib.request import urlopen
from prometheus_client.parser import text_string_to_metric_families


def parse_args() -> argparse.Namespace:
    """
    Parse command line arguments for the Prometheus metrics ingester.

    :return: Parsed arguments with the 'url' attribute.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="URL to fetch metrics from")
    return parser.parse_args()


def query_ats(url: str) -> str:
    """
    Fetch Prometheus metrics from the specified URL.

    :param url: URL to fetch metrics from.
    :return: Response text containing the metrics.
    """
    try:
        with urlopen(url) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        raise RuntimeError(f"Failed to fetch metrics from {url}: {e}")


def parse_ats_metrics(text: str) -> list:
    """
    Parse Prometheus metrics from a text string.

    :param text: The ATS output containing Prometheus metrics.
    :return: List of parsed metric families.
    """
    try:
        families = text_string_to_metric_families(text)
    except Exception as e:
        raise RuntimeError(f"Failed to parse metrics: {e}")

    if not families:
        raise RuntimeError("No metrics found in the provided text")
    return families


def print_metrics(families: list) -> None:
    """
    Print parsed metric families in Prometheus format.

    :param families: List of parsed metric families.
    """
    try:
        for family in families:
            print(f"# HELP {family.name} {family.documentation}")
            print(f"# TYPE {family.name} {family.type}")
            for sample in family.samples:
                name, labels, value = sample.name, sample.labels, sample.value
                if labels:
                    label_str = ",".join(f'{k}="{v}"' for k, v in labels.items())
                    print(f"{name}{{{label_str}}} {value}")
                else:
                    print(f"{name} {value}")
    except Exception as e:
        raise RuntimeError(f"Failed to print metrics: {e}")


def main() -> int:
    """
    Fetch and parse Prometheus metrics from a given URL.

    :return: Exit code, 0 on success, non-zero on failure.
    """
    args = parse_args()

    try:
        ats_output = query_ats(args.url)
    except RuntimeError as e:
        print(f"Error fetching URL {args.url}: {e}", file=sys.stderr)
        return 1

    try:
        families = parse_ats_metrics(ats_output)
    except RuntimeError as e:
        print(f"Error parsing ATS metrics: {e}", file=sys.stderr)
        return 1

    # Parsing issues may not arise until we try to print the metrics.
    try:
        print_metrics(families)
    except RuntimeError as e:
        print(f"Error parsing the metrics when printing them: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
