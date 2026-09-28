'''
Fetch Prometheus output from stats_over_http and verify its families and samples.
'''
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
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import re
import sys
import time

from prometheus_client.parser import text_string_to_metric_families

from fetch_stats import request
from prometheus_stats_ingester import SAMPLE_RE

METADATA_RE = re.compile(r'^# (?P<kind>HELP|TYPE) (?P<name>[a-zA-Z_:][a-zA-Z0-9_:]*) ')


@dataclass
class Family:
    help: bool = False
    type: bool = False
    samples: list[str] = field(default_factory=list)


def fetch(port: int, path: str) -> tuple[int, str]:
    response, body = request(port, path)
    return response.status, body.decode('utf-8')


def parse_families(body: str) -> dict[str, Family]:
    '''Parse the body and verify that each family is written once, with its metadata before its samples.'''
    families: dict[str, Family] = {}
    series = set()
    current = None

    for line_no, line in enumerate(body.splitlines(), 1):
        metadata = METADATA_RE.match(line)
        sample = None if metadata else SAMPLE_RE.match(line)
        if metadata is None and sample is None:
            raise RuntimeError(f'Line {line_no}: unexpected line: {line}')

        name = (metadata or sample).group('name')
        if name != current:
            if name in families:
                raise RuntimeError(f'Line {line_no}: family {name} is split')
            families[name] = Family()
            current = name
        family = families[name]

        if metadata:
            kind = metadata.group('kind').lower()
            if family.samples:
                raise RuntimeError(f'Line {line_no}: {kind.upper()} for {name} follows its samples')
            if getattr(family, kind):
                raise RuntimeError(f'Line {line_no}: {name} has a second {kind.upper()} line')
            setattr(family, kind, True)
            continue

        key = (name, sample.group('labels'))
        if key in series:
            raise RuntimeError(f'Line {line_no}: duplicate series {line}')
        series.add(key)
        family.samples.append(line)

    return families


def verify(args: argparse.Namespace, body: str) -> None:
    families = parse_families(body)
    lines = set(body.splitlines())

    for name, family in families.items():
        # The prometheus format writes current_time_epoch_ms without a HELP or TYPE line.
        if family.help != args.help and name != 'current_time_epoch_ms':
            raise RuntimeError(f'{name} should {"" if args.help else "not "}have a HELP line')
    for expected in args.expect:
        if expected not in lines:
            raise RuntimeError(f'Missing line: {expected}')
    for expected in args.family:
        name, count = expected.rsplit('=', 1)
        samples = families.get(name, Family()).samples
        if len(samples) != int(count):
            raise RuntimeError(f'{name} has {len(samples)} samples, expected {count}: {samples}')

    parsed = [family.name for family in text_string_to_metric_families(body)]
    if len(parsed) != len(set(parsed)):
        raise RuntimeError('The Prometheus parser found a family more than once')
    print(f'Verified {len(families)} families and {sum(len(f.samples) for f in families.values())} samples')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='Traffic Server HTTP port')
    parser.add_argument('path', help='the path of the stats')
    parser.add_argument('--help-lines', dest='help', action='store_true', help='every family should have a HELP line')
    parser.add_argument('--expect', action='append', default=[], help='a line that the body should have')
    parser.add_argument('--family', action='append', default=[], help='NAME=COUNT, the number of samples of a family')
    parser.add_argument('--status', type=int, default=200, help='the expected response status')
    parser.add_argument('--wait', action='store_true', help='fetch again until the status and every --expect line are there')
    parser.add_argument('--concurrency', type=int, default=1, help='send this many requests at once and verify each body')
    parser.add_argument('--fresh', action='store_true', help='fetch again, and expect another body from another render')
    args = parser.parse_args()

    try:
        if args.concurrency > 1:
            with ThreadPoolExecutor(args.concurrency) as pool:
                responses = list(pool.map(lambda _: fetch(args.port, args.path), range(args.concurrency)))
        else:
            deadline = time.monotonic() + 30
            while True:
                status, body = fetch(args.port, args.path)
                done = status == args.status and set(args.expect) <= set(body.splitlines())
                if not args.wait or done or time.monotonic() > deadline:
                    break
                time.sleep(0.2)
            responses = [(status, body)]
            if args.fresh:
                responses.append(fetch(args.port, args.path))

        for status, body in responses:
            if status != args.status:
                raise RuntimeError(f'returned status {status}, expected {args.status}')
            if status == 200:
                verify(args, body)
        if args.fresh and responses[0][1] == responses[1][1]:
            raise RuntimeError('The second response has the body of the first, so the stats were not rendered again')
    except RuntimeError as e:
        print(f'{args.path}: {e}')
        return 1
    print(f'{args.path}: status {args.status}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
