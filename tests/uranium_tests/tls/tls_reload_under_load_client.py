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
"""Drive continuous concurrent TLS handshakes against ATS while a certificate
reload (traffic_ctl config reload) happens mid-flight.

This stresses the SSL/BIO ownership boundary of the layered TLS VConnection: the
SSL context list is rebuilt while inbound handshakes and request/response writes
are in flight. A correct server keeps serving (no failed handshakes, no crash)
and the swapped-in certificate takes effect.

The script is self-contained: worker threads hammer handshakes for the whole run
while the main thread swaps the cert file and reloads ATS at a known offset, so
the reload provably overlaps in-flight handshakes.
"""

import argparse
import collections
import hashlib
import os
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time


def make_context() -> ssl.SSLContext:
    # We validate handshake mechanics and cert identity (by fingerprint), not the
    # trust chain, so disable verification to keep the client independent of which
    # signer is currently installed.
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


class Stats:

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.ok = 0
        self.fail = 0
        self.errors: collections.Counter = collections.Counter()
        self.pre_fps: collections.Counter = collections.Counter()
        self.post_fps: collections.Counter = collections.Counter()


def worker(
        args: argparse.Namespace, stats: Stats, stop: threading.Event, reload_started: threading.Event,
        reload_settled: threading.Event) -> None:
    ctx = make_context()
    while not stop.is_set():
        try:
            with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
                with ctx.wrap_socket(sock, server_hostname=args.sni) as ssock:
                    der = ssock.getpeercert(binary_form=True)
                    fp = hashlib.sha256(der).hexdigest()[:16] if der else "none"
                    request = (f"GET / HTTP/1.1\r\nHost: {args.sni}\r\n"
                               "Connection: close\r\n\r\n").encode()
                    ssock.sendall(request)
                    resp = ssock.recv(256)
            if not resp.startswith(b"HTTP/1."):
                with stats.lock:
                    stats.fail += 1
                    stats.errors["bad_response"] += 1
                continue
            with stats.lock:
                stats.ok += 1
                # Bucket the served cert by phase. Ignore the transition window so
                # pre==v1 and post==v2 cleanly, regardless of reload latency.
                if not reload_started.is_set():
                    stats.pre_fps[fp] += 1
                elif reload_settled.is_set():
                    stats.post_fps[fp] += 1
        except Exception as exc:  # noqa: BLE001 - any failure is a test signal
            with stats.lock:
                stats.fail += 1
                stats.errors[type(exc).__name__] += 1


def do_reload(env_traffic_ctl: str) -> None:
    subprocess.run([env_traffic_ctl, "config", "reload"], check=True, timeout=30)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, required=True)
    parser.add_argument("--sni", default="bar.com")
    parser.add_argument("--ssldir", required=True, help="dir holding the live cert that gets swapped")
    parser.add_argument("--live-cert", default="signed-bar.pem", help="cert filename inside --ssldir")
    parser.add_argument("--v2-cert", required=True, help="source cert to copy over the live cert at reload")
    parser.add_argument("--traffic-ctl", default="traffic_ctl")
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--concurrency", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--reload-at", type=float, default=2.0)
    parser.add_argument("--reloads", type=int, default=3)
    parser.add_argument("--reload-spacing", type=float, default=0.7)
    parser.add_argument("--settle", type=float, default=0.8, help="grace after last reload before sampling post cert")
    parser.add_argument("--min-ok", type=int, default=20)
    args = parser.parse_args()

    stats = Stats()
    stop = threading.Event()
    reload_started = threading.Event()
    reload_settled = threading.Event()

    workers = [
        threading.Thread(target=worker, args=(args, stats, stop, reload_started, reload_settled), daemon=True)
        for _ in range(args.concurrency)
    ]
    start = time.monotonic()
    for t in workers:
        t.start()

    # Let load ramp up so the reload lands on top of in-flight handshakes.
    time.sleep(args.reload_at)

    live_path = os.path.join(args.ssldir, args.live_cert)
    reloads_done = 0
    reload_error = ""
    try:
        # Swap to v2 once; the first reload installs it. Further reloads keep
        # rebuilding the SSL store under sustained load.
        shutil.copyfile(args.v2_cert, live_path)
        os.utime(live_path, None)
        reload_started.set()
        for i in range(args.reloads):
            do_reload(args.traffic_ctl)
            reloads_done += 1
            if i != args.reloads - 1:
                time.sleep(args.reload_spacing)
    except Exception as exc:  # noqa: BLE001
        reload_error = f"{type(exc).__name__}: {exc}"

    time.sleep(args.settle)
    reload_settled.set()

    elapsed = time.monotonic() - start
    if args.duration > elapsed:
        time.sleep(args.duration - elapsed)
    stop.set()
    for t in workers:
        t.join(timeout=args.timeout + 2)

    pre_fp = stats.pre_fps.most_common(1)[0][0] if stats.pre_fps else "none"
    post_fp = stats.post_fps.most_common(1)[0][0] if stats.post_fps else "none"
    cert_changed = int(pre_fp != "none" and post_fp != "none" and pre_fp != post_fp)

    passed = (
        reload_error == "" and reloads_done == args.reloads and stats.fail == 0 and stats.ok >= args.min_ok and cert_changed == 1)

    print(f"HANDSHAKES_OK={stats.ok}")
    print(f"FAILURES={stats.fail}")
    print(f"RELOADS_DONE={reloads_done}")
    print(f"CERT_CHANGED={cert_changed}")
    print(f"PRE_FP={pre_fp} POST_FP={post_fp}")
    if stats.errors:
        print(f"ERROR_BREAKDOWN={dict(stats.errors)}")
    if reload_error:
        print(f"RELOAD_ERROR={reload_error}")
    print(f"RESULT={'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
