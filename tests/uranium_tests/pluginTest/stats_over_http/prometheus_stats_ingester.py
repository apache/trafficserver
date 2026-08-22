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
from collections import Counter
import re
import sys
from urllib.request import urlopen
from prometheus_client.parser import text_string_to_metric_families

HELP_RE = re.compile(r"^# HELP (?P<name>[a-zA-Z_:][a-zA-Z0-9_:]*) (?P<doc>.*)$")
TYPE_RE = re.compile(r"^# TYPE (?P<name>[a-zA-Z_:][a-zA-Z0-9_:]*) (?P<type>[a-zA-Z]+)$")
SAMPLE_RE = re.compile(
    r"^(?P<name>[a-zA-Z_:][a-zA-Z0-9_:]*)(?:\{(?P<labels>.*)\})?\s+"
    r"(?P<value>(?:[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)|(?:[+-]?(?:Inf|inf))|(?:NaN|nan))$")
LABEL_RE = re.compile(r'(?P<name>[a-zA-Z_][a-zA-Z0-9_]*)="(?P<value>(?:\\.|[^"\\])*)"')


def parse_args() -> argparse.Namespace:
    """
    Parse command line arguments for the Prometheus metrics ingester.

    :return: Parsed arguments with the 'url' attribute.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict-family-metadata",
        action="store_true",
        help="Fail if parsed metric families are split or emitted without TYPE metadata.",
    )
    parser.add_argument(
        "--validate-v2-format",
        action="store_true",
        help="Fail if the raw v2 exposition is not grouped into complete, labeled metric families.",
    )
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
        families = list(text_string_to_metric_families(text))
    except Exception as e:
        raise RuntimeError(f"Failed to parse metrics: {e}")

    if not families:
        raise RuntimeError("No metrics found in the provided text")
    return families


def validate_metric_families(families: list) -> None:
    """
    Verify each metric family is complete and emitted once.

    Prometheus' parser accepts samples without adjacent HELP/TYPE metadata by
    parsing them as unknown families. That is useful leniency, but it can hide
    broken exposition output where samples for one metric family are interleaved
    with unrelated metrics.

    :param families: List of parsed metric families.
    """
    family_counts = Counter(family.name for family in families)
    duplicate_families = sorted(name for name, count in family_counts.items() if count > 1)
    if duplicate_families:
        raise RuntimeError(f"Duplicate metric families found: {', '.join(duplicate_families)}")

    unknown_families = sorted(family.name for family in families if family.type == "unknown")
    if unknown_families:
        raise RuntimeError(f"Metric families without TYPE metadata found: {', '.join(unknown_families)}")


def decode_label_value(value: str, line_no: int) -> str:
    """
    Decode a Prometheus label value and reject unsupported escape sequences.

    :param value: Escaped label value without the surrounding quotes.
    :param line_no: Line number used for diagnostics.
    :return: Decoded label value.
    """
    decoded = []
    i = 0
    while i < len(value):
        if value[i] != "\\":
            decoded.append(value[i])
            i += 1
            continue

        if i + 1 >= len(value):
            raise RuntimeError(f"Line {line_no}: label value ends with an incomplete escape")

        escaped = value[i + 1]
        if escaped == "n":
            decoded.append("\n")
        elif escaped in {'"', "\\"}:
            decoded.append(escaped)
        else:
            raise RuntimeError(f"Line {line_no}: unsupported label escape \\{escaped}")
        i += 2

    return "".join(decoded)


def parse_labels(labels_text: str | None, line_no: int) -> dict[str, str]:
    """
    Parse a Prometheus label block.

    :param labels_text: Label block without braces, or None if absent.
    :param line_no: Line number used for diagnostics.
    :return: Mapping of label names to decoded values.
    """
    if labels_text is None:
        return {}
    if not labels_text:
        raise RuntimeError(f"Line {line_no}: empty label block")

    labels = {}
    pos = 0
    while pos < len(labels_text):
        match = LABEL_RE.match(labels_text, pos)
        if match is None:
            raise RuntimeError(f"Line {line_no}: invalid label syntax near: {labels_text[pos:]}")

        name = match.group("name")
        if name in labels:
            raise RuntimeError(f"Line {line_no}: duplicate label {name}")
        labels[name] = decode_label_value(match.group("value"), line_no)

        pos = match.end()
        if pos == len(labels_text):
            break
        if labels_text[pos] != ",":
            raise RuntimeError(f"Line {line_no}: expected comma after label {name}")
        pos += 1
        if pos < len(labels_text) and labels_text[pos] == " ":
            pos += 1
        if pos == len(labels_text):
            raise RuntimeError(f"Line {line_no}: trailing comma in label block")

    return labels


def require_family(samples_by_family: dict[str, list[dict[str, str]]], family: str) -> list[dict[str, str]]:
    """
    Retrieve samples for a required metric family.

    :param samples_by_family: Mapping of family names to their parsed labels.
    :param family: Required family name.
    :return: Samples for the family.
    """
    try:
        return samples_by_family[family]
    except KeyError:
        raise RuntimeError(f"Required metric family missing: {family}")


def require_label_values(samples: list[dict[str, str]], family: str, label: str, expected_values: set[str]) -> None:
    """
    Verify a family has samples for each expected value of a label.

    :param samples: Parsed labels for all samples in the family.
    :param family: Family name used for diagnostics.
    :param label: Label name to inspect.
    :param expected_values: Required label values.
    """
    actual_values = {sample[label] for sample in samples if label in sample}
    missing_values = sorted(expected_values - actual_values)
    if missing_values:
        raise RuntimeError(f"{family} is missing {label} values: {', '.join(missing_values)}")


def require_sample(samples: list[dict[str, str]], family: str, required_labels: dict[str, str]) -> None:
    """
    Verify a family has a sample containing a set of labels.

    :param samples: Parsed labels for all samples in the family.
    :param family: Family name used for diagnostics.
    :param required_labels: Required labels and values.
    """
    for sample in samples:
        if all(sample.get(label) == value for label, value in required_labels.items()):
            return

    labels = ", ".join(f'{label}="{value}"' for label, value in required_labels.items())
    raise RuntimeError(f"{family} is missing a sample with labels: {labels}")


def validate_prometheus_v2_label_coverage(samples_by_family: dict[str, list[dict[str, str]]]) -> None:
    """
    Verify the v2 output exercises the expected label transformations.

    :param samples_by_family: Mapping of family names to their parsed labels.
    """
    http_request_samples = require_family(samples_by_family, "proxy_process_http_requests")
    require_label_values(
        http_request_samples,
        "proxy_process_http_requests",
        "method",
        {
            "connect",
            "delete",
            "extension_method",
            "get",
            "head",
            "invalid_client",
            "options",
            "post",
            "purge",
            "push",
            "put",
            "trace",
        },
    )
    require_label_values(http_request_samples, "proxy_process_http_requests", "direction", {"incoming", "outgoing"})
    for sample in http_request_samples:
        if set(sample) not in ({"method"}, {"direction"}):
            raise RuntimeError(f"proxy_process_http_requests has unexpected labels: {sample}")

    completed_samples = require_family(samples_by_family, "proxy_process_http_completed_requests")
    for sample in completed_samples:
        if sample:
            raise RuntimeError("proxy_process_http_completed_requests should not have labels")

    response_samples = require_family(samples_by_family, "proxy_process_http_responses")
    require_label_values(response_samples, "proxy_process_http_responses", "direction", {"incoming"})
    require_label_values(
        response_samples,
        "proxy_process_http_responses",
        "status",
        {"000", "100", "1xx", "200", "2xx", "404", "4xx", "500", "5xx"},
    )

    require_sample(
        require_family(samples_by_family, "proxy_process_http_disallowed_continue"),
        "proxy_process_http_disallowed_continue",
        {
            "method": "post",
            "status": "100"
        },
    )
    require_label_values(
        require_family(samples_by_family, "proxy_process_http_cache_ims"), "proxy_process_http_cache_ims", "result",
        {"hit", "miss"})
    require_label_values(
        require_family(samples_by_family, "proxy_process_http_cache_fresh"), "proxy_process_http_cache_fresh", "result", {"hit"})
    require_sample(
        require_family(samples_by_family, "proxy_process_http_transaction_counts_failed"),
        "proxy_process_http_transaction_counts_failed",
        {
            "result": "errors",
            "method": "connect"
        },
    )
    require_label_values(
        require_family(samples_by_family, "proxy_process_eventloop_count"), "proxy_process_eventloop_count", "le",
        {"10s", "100s", "1000s"})
    require_label_values(
        require_family(samples_by_family, "proxy_process_eventloop_time"), "proxy_process_eventloop_time", "le",
        {"0ms", "100ms", "2560ms"})
    require_label_values(
        require_family(samples_by_family, "proxy_process_cache_volume_lookup_active"), "proxy_process_cache_volume_lookup_active",
        "volume", {"0"})
    require_label_values(
        require_family(samples_by_family, "proxy_process_cache_volume_lookup_success"), "proxy_process_cache_volume_lookup_success",
        "volume", {"0"})

    for family, samples in samples_by_family.items():
        for sample in samples:
            if sample.get("method") == "completed":
                raise RuntimeError(f"{family} incorrectly labels completed as an HTTP method")


def validate_prometheus_v2_text(text: str) -> None:
    """
    Verify the raw v2 exposition has complete grouped metric families.

    :param text: Raw ATS Prometheus v2 output.
    """
    current_name = None
    current_type = None
    current_has_sample = False
    seen_families = set()
    samples_by_family = {}
    help_count = 0
    type_count = 0
    sample_count = 0

    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            continue

        help_match = HELP_RE.match(line)
        if help_match is not None:
            if current_name is not None and not current_has_sample:
                raise RuntimeError(f"Line {line_no}: family {current_name} has no samples")

            current_name = help_match.group("name")
            if current_name in seen_families:
                raise RuntimeError(f"Line {line_no}: duplicate HELP for metric family {current_name}")
            seen_families.add(current_name)
            samples_by_family[current_name] = []
            current_type = None
            current_has_sample = False
            help_count += 1
            continue

        type_match = TYPE_RE.match(line)
        if type_match is not None:
            type_name = type_match.group("name")
            if current_name is None:
                raise RuntimeError(f"Line {line_no}: TYPE appears before HELP")
            if type_name != current_name:
                raise RuntimeError(f"Line {line_no}: TYPE name {type_name} does not match HELP name {current_name}")
            if current_type is not None:
                raise RuntimeError(f"Line {line_no}: duplicate TYPE for metric family {current_name}")

            current_type = type_match.group("type")
            if current_type not in ("counter", "gauge"):
                raise RuntimeError(f"Line {line_no}: unsupported TYPE for {current_name}: {current_type}")
            type_count += 1
            continue

        if line.startswith("#"):
            raise RuntimeError(f"Line {line_no}: unsupported metadata line: {line}")

        sample_match = SAMPLE_RE.match(line)
        if sample_match is None:
            raise RuntimeError(f"Line {line_no}: invalid sample line: {line}")
        if current_name is None or current_type is None:
            raise RuntimeError(f"Line {line_no}: sample appears before HELP/TYPE")

        sample_name = sample_match.group("name")
        expected_names = {current_name}
        if current_type == "counter":
            expected_names.add(f"{current_name}_total")
        if sample_name not in expected_names:
            raise RuntimeError(f"Line {line_no}: sample {sample_name} does not belong to family {current_name}")

        labels = parse_labels(sample_match.group("labels"), line_no)
        samples_by_family[current_name].append(labels)
        current_has_sample = True
        sample_count += 1

    if help_count == 0:
        raise RuntimeError("No metric families found")
    if current_name is not None and not current_has_sample:
        raise RuntimeError(f"Metric family {current_name} has no samples")
    if help_count != type_count:
        raise RuntimeError(f"HELP/TYPE count mismatch: {help_count} HELP lines, {type_count} TYPE lines")
    if sample_count < help_count:
        raise RuntimeError(f"Expected at least one sample per family, saw {sample_count} samples for {help_count} families")

    validate_prometheus_v2_label_coverage(samples_by_family)


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

    if args.validate_v2_format:
        try:
            validate_prometheus_v2_text(ats_output)
        except RuntimeError as e:
            print(f"Error validating Prometheus v2 metrics: {e}", file=sys.stderr)
            return 1

    try:
        families = parse_ats_metrics(ats_output)
    except RuntimeError as e:
        print(f"Error parsing ATS metrics: {e}", file=sys.stderr)
        return 1

    if args.strict_family_metadata:
        try:
            validate_metric_families(families)
        except RuntimeError as e:
            print(f"Error validating metric families: {e}", file=sys.stderr)
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
