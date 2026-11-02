'''
Drive stats_over_http requests that wait for a render on a task thread, and check the render in traffic.out.
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
import http.client
import json
import os
import re
import socket
import struct
import subprocess
import sys
import threading
import time

from fetch_stats import decode, request, test_metric_values

WAITING = r'Waiting for a render of stats instance (?P<instance>0x[0-9a-f]+)'


def rendered(instance: str = r'(?P<instance>0x[0-9a-f]+)', count: str = r'(?P<count>\d+)') -> str:
    '''The log line of a render, which runs on a task thread.'''
    return rf'\[ET_TASK \d+\] .*Rendered stats instance {instance} for {count} waiting requests'


RENDERED = rendered()


class Failure(Exception):
    pass


class Log:
    '''Reads traffic.out from an offset, so that each check sees only the lines of its own scenario.'''

    def __init__(self, path: str):
        self.path = path
        self.offset = os.path.getsize(path)

    def text(self) -> str:
        with open(self.path, 'rb') as f:
            f.seek(self.offset)
            return f.read().decode(errors='replace')

    def wait_for(self, pattern: str, timeout: float = 30) -> re.Match:
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, self.text())
            if match:
                return match
            if time.monotonic() > deadline:
                raise Failure(f'traffic.out has no match for {pattern!r} after {timeout} s')
            time.sleep(0.05)

    def all(self, pattern: str) -> list[re.Match]:
        return list(re.finditer(pattern, self.text()))


class Client:

    def __init__(self, args: argparse.Namespace):
        self.args = args

    def get(self, path: str, encoding: str | None = None, timeout: float = 60) -> tuple[int, http.client.HTTPMessage, bytes]:
        response, body = request(self.args.port, path, encoding, timeout=timeout)
        return response.status, response.headers, body

    def verify(self, path: str, stats_format: str, encoding: str | None = None) -> None:
        status, headers, body = self.get(path, encoding)
        if status != 200:
            raise Failure(f'{path} returned status {status}')
        if headers.get('content-encoding') != encoding:
            raise Failure(f'{path} returned Content-Encoding {headers.get("content-encoding")}, expected {encoding}')
        values = test_metric_values(stats_format, decode(encoding, body).decode('utf-8'))
        if values != list(range(self.args.count)):
            raise Failure(f'{path} with {encoding} has {len(values)} test metrics, expected {self.args.count}')

    def render_time(self, path: str) -> int:
        '''GET the JSON stats and return their current_time_epoch_ms, which each render sets.'''
        status, _, body = self.get(path)
        if status != 200:
            raise Failure(f'{path} returned status {status}')
        return int(json.loads(body)['global']['current_time_epoch_ms'])

    def stall(self, ms: int, log: Log | None = None) -> Log:
        '''Hold up the record dumps of the renders for ms milliseconds, and return the log from before the stall.'''
        log = log or Log(self.args.traffic_out)
        subprocess.run(['traffic_ctl', 'plugin', 'msg', 'test_metrics.stall', str(ms)], check=True, capture_output=True)
        log.wait_for(f'Stalling record dumps for {ms} ms')
        return log

    def hold_task(self, ms: int, log: Log) -> re.Match:
        '''Queue a task that keeps a task thread busy for ms milliseconds, and return its log line.'''
        subprocess.run(['traffic_ctl', 'plugin', 'msg', 'test_metrics.hold_task', str(ms)], check=True, capture_output=True)
        return log.wait_for(f'Queued a task that holds a task thread for {ms} ms')

    def metric(self, name: str) -> int:
        out = subprocess.run(['traffic_ctl', 'metric', 'get', name], check=True, capture_output=True, text=True).stdout
        return int(out.split()[-1])

    def reload_remap(self) -> None:
        # Traffic Server reloads a file only when its modification time changes.
        os.utime(self.args.remap_config)
        subprocess.run(
            ['traffic_ctl', 'config', 'reload', '--monitor', '--initial-wait=0.1', '--refresh-int=0.1', '--timeout=60s'],
            check=True,
            capture_output=True)


def concurrent(client: Client) -> None:
    '''Requests that arrive during a render all wait for it, and one render answers them all.'''
    before = client.metric('plugin.stats_over_http.renders')
    log = client.stall(3000)
    with ThreadPoolExecutor(client.args.requests) as pool:
        list(pool.map(lambda _: client.verify(client.args.path, 'prometheus'), range(client.args.requests)))
    renders = log.all(RENDERED)
    if [int(render['count']) for render in renders] != [client.args.requests]:
        raise Failure(f'Expected one render for {client.args.requests} requests: {[render[0] for render in renders]}')
    counted = client.metric('plugin.stats_over_http.renders') - before
    if counted != 1:
        raise Failure(f'plugin.stats_over_http.renders counted {counted} renders, expected 1')
    print(f'One render answered {client.args.requests} concurrent requests')


def reuse(client: Client) -> None:
    '''A render answers later requests until it is older than --max-age-ms.  The next request then renders again.'''
    max_age = client.args.max_age_ms / 1000
    log = Log(client.args.traffic_out)
    start = time.monotonic()
    first = client.render_time(client.args.path)
    instance = log.wait_for(WAITING)['instance']
    log.wait_for(rendered(instance, '1'))

    # The first render happened after start, so a request answered before start + max_age finds it younger than max_age.
    second = client.render_time(client.args.path)
    if time.monotonic() - start >= max_age:
        raise Failure(f'The second request took longer than {max_age} s, so it cannot show the reuse of the render')
    if second != first:
        raise Failure(f'A request within --max-age-ms got a new render: current_time_epoch_ms {first}, then {second}')

    deadline = time.monotonic() + max_age + 30
    while (later := client.render_time(client.args.path)) == first:
        if time.monotonic() > deadline:
            raise Failure(f'No new render {max_age + 30} s after the first one')
        time.sleep(0.1)
    elapsed = time.monotonic() - start
    if elapsed < max_age:
        raise Failure(f'A new render answered a request {elapsed:.2f} s after the first request, within --max-age-ms')

    counts = [render['count'] for render in log.all(rendered(instance))]
    if counts != ['1', '1']:
        raise Failure(f'Expected one render for the first request and one after --max-age-ms: {counts}')
    print(f'The render at {first} answered the second request, and the render at {later} answered a request after {elapsed:.2f} s')


def abort(client: Client, reset: bool) -> None:
    '''A client that goes away while its request waits for a render leaves nothing behind.'''
    log = client.stall(3000)
    sock = socket.create_connection(('127.0.0.1', client.args.port))
    sock.sendall(f'GET {client.args.path} HTTP/1.1\r\nHost: 127.0.0.1:{client.args.port}\r\n\r\n'.encode())
    instance = log.wait_for(WAITING)['instance']
    if reset:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    sock.close()

    render = log.wait_for(rendered(instance, '1'))
    if reset:
        # The rule turns off half-open connections, so Traffic Server releases the intercept (NET_ACCEPT_FAILED) when
        # the client closes the connection while the transaction waits.  The transaction closes after the render
        # reenables it.
        closed = log.wait_for(r'Intercept finished on (?P<event>TS_EVENT_NET_ACCEPT_FAILED)')
        if closed.start() > render.start():
            raise Failure('The intercept of the reset request finished after the render')
    else:
        # The transaction keeps waiting for the render, then writes to a client that has gone.
        closed = log.wait_for(r'Intercept finished on (?P<event>\S+)')
        if closed.start() < render.start():
            raise Failure('The intercept of the closed request finished before the render')
    print(f'The render answered the {"reset" if reset else "closed"} request, whose intercept finished on {closed["event"]}')
    client.verify(client.args.path, 'json')


def timeout_then_reload(client: Client) -> None:
    '''The watchdog answers a request that waits too long, and a remap reload deletes the rule while the render runs.

    The render then holds the last reference to the instance, and frees it when it finishes.  The reload and the render run
    on task threads, so the reload can queue behind the render on the same thread.  Try again when that happens.
    '''
    for attempt in range(1, 6):
        log = client.stall(6000)
        start = time.monotonic()
        status, headers, body = client.get(client.args.path)
        elapsed = time.monotonic() - start
        if status != 503 or headers.get('x-stats-format') != 'prometheus' or headers.get('content-length') != '0' or body:
            raise Failure(f'Expected an empty 503 from the watchdog, got {status} {headers} {body[:100]!r}')
        if elapsed < client.args.wait_timeout_ms / 1000 - 0.1:
            raise Failure(f'The 503 arrived after {elapsed:.2f} s, before the wait timeout')
        instance = log.wait_for(WAITING)['instance']
        log.wait_for(
            rf'Answering 1 requests with a 503 after {client.args.wait_timeout_ms} ms without a render of stats instance {instance}'
        )

        client.reload_remap()
        released = log.wait_for(rf'Releasing remap instance {instance}')
        render = log.wait_for(rendered(instance, '0'))
        freed = log.wait_for(rf'Freeing stats instance {instance}')
        if released.start() < render.start() < freed.start():
            print(f'Attempt {attempt}: the render freed stats instance {instance} after the reload released it')
            log.wait_for('Stopped stalling record dumps')
            return
        print(f'Attempt {attempt}: the reload released stats instance {instance} after the render, trying again')
        log.wait_for('Stopped stalling record dumps')
    raise Failure('The reload never released the instance while the render ran')


def reload_loop(client: Client) -> None:
    '''Remap reloads replace the instance while requests keep it rendering.'''
    log = Log(client.args.traffic_out)
    stop = threading.Event()
    statuses: list[int] = []
    errors: list[str] = []

    def scrape() -> None:
        while not stop.is_set():
            try:
                client.verify(client.args.path, 'prometheus')
                statuses.append(200)
            except Exception as e:
                errors.append(str(e))

    scrapers = [threading.Thread(target=scrape) for _ in range(4)]
    for scraper in scrapers:
        scraper.start()
    for _ in range(client.args.reloads):
        client.reload_remap()
    stop.set()
    for scraper in scrapers:
        scraper.join()

    instances = {render['instance'] for render in log.all(RENDERED)}
    released = log.all(r'Releasing remap instance')
    if errors:
        raise Failure(f'{len(errors)} of {len(errors) + len(statuses)} requests failed, the first: {errors[0]}')
    if len(instances) < 2 or not released:
        raise Failure(
            f'The reloads should replace the rendering instance: {len(instances)} instances rendered, '
            f'{len(released)} released')
    print(f'{len(statuses)} requests during {client.args.reloads} reloads, {len(instances)} instances rendered')


def global_mix(client: Client) -> None:
    '''One render answers the global plugin's requests for each format and encoding that arrive while it runs.'''
    encodings = ['gzip', 'deflate'] + (['br'] if client.args.brotli else [])
    later = [
        ('csv', None),
        ('prometheus', 'gzip'),
        ('prometheus_v2', None),
        ('json', encodings[-1]),
        ('json', 'deflate'),
        ('csv', 'gzip'),
        ('prometheus', None),
    ]
    log = client.stall(3000)
    with ThreadPoolExecutor(len(later) + 1) as pool:
        first = pool.submit(client.verify, '/_stats/json', 'json')
        log.wait_for(WAITING)
        rest = [pool.submit(client.verify, f'/_stats/{fmt}', fmt, encoding) for fmt, encoding in later]
        for future in [first, *rest]:
            future.result()
    counts = [int(render['count']) for render in log.all(RENDERED)]
    if counts != [1, len(later)]:
        raise Failure(f'Expected a render for the first request, then one for the other {len(later)}: {counts}')
    print(f'Two renders answered {len(later) + 1} requests for {len(later) + 1} formats and encodings')


def global_timeout(client: Client) -> None:
    '''The watchdog answers a request to the global plugin that waits too long with an empty 503.'''
    timeout = client.args.wait_timeout_ms / 1000
    log = client.stall(client.args.wait_timeout_ms + 1000)
    start = time.monotonic()
    status, headers, body = client.get('/_stats/json')
    elapsed = time.monotonic() - start
    if status != 503 or body or 'content-type' in headers or 'x-stats-format' in headers:
        raise Failure(f'Expected an empty 503 without stats headers from the watchdog, got {status} {headers} {body[:100]!r}')
    if elapsed < timeout - 0.1:
        raise Failure(f'The 503 arrived after {elapsed:.2f} s, before the wait timeout')
    instance = log.wait_for(WAITING)['instance']
    log.wait_for(
        rf'Answering 1 requests with a 503 after {client.args.wait_timeout_ms} ms without a render of stats instance {instance}')
    log.wait_for(rendered(instance, '0'))
    client.verify('/_stats/json', 'json')
    print(f'The watchdog answered after {elapsed:.2f} s, and the next request got the stats')


def global_handover(client: Client) -> None:
    '''A request that waits for the render after the one in flight gets a wait timeout that starts when that render ends.

    A render schedules the next render on its own task thread, ahead of the work that other threads queue after the
    render starts.  With one task thread and a wait timeout W:

      0       A held task keeps the thread busy until W/2.  The first request waits, and its render queues behind the
              held task.  A second held task of 5/8 W queues behind the render.
      W/2     The render starts, and a stall holds it until 3/4 W.  A request for another format arrives and waits.
      3/4 W   The render answers the first request and schedules the next render behind the second held task.  The
              wait timeout of the second request starts now and ends at 7/4 W.
      11/8 W  The second held task ends, and the next render answers the second request.  This is after W, when the
              wait timeout of the first request would end, and before 7/4 W.
    '''
    timeout = client.args.wait_timeout_ms
    log = Log(client.args.traffic_out)
    client.hold_task(timeout // 2, log)
    log.wait_for(f'Holding a task thread for {timeout // 2} ms')
    client.stall(timeout * 3 // 4, log)
    with ThreadPoolExecutor(2) as pool:
        first = pool.submit(client.verify, '/_stats/json', 'json')
        log.wait_for(WAITING)
        queued = client.hold_task(timeout * 5 // 8, log)
        released = log.wait_for('Released the task thread')
        if queued.start() > released.start():
            raise Failure('The second held task was queued after the first one ended, so the host was too slow for this scenario')
        second = pool.submit(client.verify, '/_stats/csv', 'csv')
        log.wait_for(r'Waiting for a render[^\n]*\n[\s\S]*Waiting for a render')
        for future in [first, second]:
            future.result()
    held = log.wait_for(rf'Holding a task thread for {timeout * 5 // 8} ms')

    renders = log.all(RENDERED)
    if [int(render['count']) for render in renders] != [1, 1]:
        raise Failure(f'Expected a render for each request: {[render[0] for render in renders]}')
    if not renders[0].start() < held.start() < renders[1].start():
        raise Failure('The second held task did not run between the renders')
    if timeouts := log.all(r'Answering \d+ requests with a 503'):
        raise Failure(f'The watchdog answered a request: {timeouts[0][0]}')
    log.wait_for(r'Released the task thread[\s\S]*Released the task thread')
    print('The second render answered its request after the wait timeout of the first request')


def global_fresh(client: Client) -> None:
    '''With --max-age-ms=0, a request that arrives during a render waits for the next render.

    The log shows the order of the renders.  Two renders can finish in the same millisecond, so their current_time_epoch_ms
    values can be equal and cannot show that order.
    '''
    log = client.stall(client.args.wait_timeout_ms // 2)
    with ThreadPoolExecutor(2) as pool:
        first = pool.submit(client.verify, '/_stats/json', 'json')
        log.wait_for(WAITING)
        second = pool.submit(client.verify, '/_stats/json', 'json')
        waiting = log.wait_for(r'Waiting for a render[^\n]*\n[\s\S]*Waiting for a render')
        for future in [first, second]:
            future.result()
    renders = log.all(RENDERED)
    if [int(render['count']) for render in renders] != [1, 1]:
        raise Failure(f'Expected a render for each request: {[render[0] for render in renders]}')
    if waiting.end() > renders[0].start():
        raise Failure('The second request arrived after the first render ended, so the host was too slow for this scenario')
    print('The second request arrived during the first render, which answered one request, and the next render answered the other')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='Traffic Server HTTP port')
    parser.add_argument('traffic_out', help='the traffic.out of Traffic Server')
    parser.add_argument(
        'scenario',
        choices=[
            'concurrent', 'reuse', 'reset', 'close', 'timeout-reload', 'reload-loop', 'global', 'global-timeout', 'global-handover',
            'global-fresh'
        ],
        help='what to run')
    parser.add_argument('--path', default='/', help='the path of the remap rule')
    parser.add_argument('--count', type=int, required=True, help='the --count given to test_metrics.so')
    parser.add_argument('--requests', type=int, default=50, help='the number of concurrent requests')
    parser.add_argument('--reloads', type=int, default=10, help='the number of remap reloads')
    parser.add_argument('--remap-config', help='the remap.config to touch before a reload')
    parser.add_argument('--brotli', action='store_true', help='Traffic Server supports br')
    parser.add_argument('--max-age-ms', type=int, help='the --max-age-ms of the stats')
    parser.add_argument('--wait-timeout-ms', type=int, help='the --wait-timeout-ms of the stats')
    args = parser.parse_args()
    client = Client(args)

    try:
        if args.scenario == 'concurrent':
            concurrent(client)
        elif args.scenario == 'reuse':
            reuse(client)
        elif args.scenario in ('reset', 'close'):
            abort(client, reset=args.scenario == 'reset')
        elif args.scenario == 'timeout-reload':
            timeout_then_reload(client)
        elif args.scenario == 'reload-loop':
            reload_loop(client)
        elif args.scenario == 'global-timeout':
            global_timeout(client)
        elif args.scenario == 'global-handover':
            global_handover(client)
        elif args.scenario == 'global-fresh':
            global_fresh(client)
        else:
            global_mix(client)
    except Failure as e:
        print(f'{args.scenario}: {e}')
        return 1
    print(f'{args.scenario}: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
