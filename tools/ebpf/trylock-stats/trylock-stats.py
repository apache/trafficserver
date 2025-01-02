#!/usr/bin/env python
#
#  bcc tool to observe pthread mutex trylock
#
#  Based on https://github.com/goldshtn/linux-tracing-workshop/blob/master/lockstat-solution.py
#
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

import os
import subprocess
import sys
import itertools
from time import sleep
from bcc import BPF
from argparse import ArgumentParser

BPF_SRC_FILE = "./trylock-stats.bpf.c"


def glibc_version(glibc_path):
    major = 0
    minor = 0

    result = subprocess.run([glibc_path], capture_output=True, text=True)

    if result:
        version_str = result.stdout.splitlines()[0].split()[-1]

        version = version_str.split(".")
        major = int(version[0])
        minor = int(version[1])

    return (major, minor)


def attach(bpf, pid, glibc_path):
    libname = "pthread"

    # glibc removed "libpthread" from 2.34
    (major, minor) = glibc_version(glibc_path)
    if major >= 2 and minor >= 34:
        libname = "c"

    bpf.attach_uprobe(name=libname, sym="pthread_mutex_trylock", fn_name="probe_mutex_lock", pid=pid)
    bpf.attach_uretprobe(name=libname, sym="pthread_mutex_trylock", fn_name="probe_mutex_trylock_return", pid=pid)
    bpf.attach_uprobe(name=libname, sym="pthread_mutex_unlock", fn_name="probe_mutex_unlock", pid=pid)


def print_frame(bpf, pid, addr):
    print("\t\t%16s (%x)" % (bpf.sym(addr, pid, show_module=True, show_offset=True), addr))


def print_stack(bpf, pid, stacks, stack_id):
    for addr in stacks.walk(stack_id):
        print_frame(bpf, pid, addr)


def run(args):
    pid = args.pid
    bpf = BPF(src_file=BPF_SRC_FILE)
    attach(bpf, pid, args.glibc_path)

    init_stacks = bpf["init_stacks"]
    stacks = bpf["stacks"]
    locks = bpf["locks"]
    mutex_lock_hist = bpf["mutex_lock_hist"]
    mutex_wait_hist = bpf["mutex_wait_hist"]

    sleep(args.duration)

    mutex_ids = {}
    next_mutex_id = 1
    for k, v in init_stacks.items():
        mutex_id = "#%d" % next_mutex_id
        next_mutex_id += 1
        mutex_ids[k.value] = mutex_id
        print("init stack for mutex %x (%s)" % (k.value, mutex_id))
        print_stack(bpf, pid, stacks, v.value)
        print("")

    grouper = lambda kv: kv[0].tid
    sorted_by_thread = sorted(locks.items(), key=grouper)
    locks_by_thread = itertools.groupby(sorted_by_thread, grouper)

    for tid, items in locks_by_thread:
        print("thread %d" % tid)

        for k, v in sorted(items, key=lambda kv: -kv[1].fail_count):
            mutex_descr = mutex_ids[k.mtx] if k.mtx in mutex_ids else bpf.sym(k.mtx, pid)
            print(
                "\tmutex %s ::: wait time %.2fus ::: hold time %.2fus ::: enter count %d ::: try-lock failure count %d" %
                (mutex_descr, v.wait_time_ns / 1000.0, v.lock_time_ns / 1000.0, v.enter_count, v.fail_count))
            print_stack(bpf, pid, stacks, k.lock_stack_id)
            print("")

    mutex_wait_hist.print_log2_hist(val_type="wait time (us)")
    mutex_lock_hist.print_log2_hist(val_type="hold time (us)")


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("-p", "--pid", dest="pid", help="process id", type=int, required=True)
    parser.add_argument("-d", "--duration", dest="duration", help="duration to run", default=10, type=float)
    parser.add_argument("-l", "--glibc", dest="glibc_path", help="path to the glibc", default="/lib64/libc.so.6")

    run(parser.parse_args())
