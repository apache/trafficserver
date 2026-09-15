#!/usr/bin/env bash
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
#
# Drive the rate_limit SNI limiter's queue-then-resume path with exactly one queued connection,
# and check that the active-slot counter stays balanced and the server survives.
#
#   1. holder completes its handshake and holds the single slot (counter = 1);
#   2. one connection enqueues because the slot is full and stays parked at the ClientHello hook;
#   3. the sweep runs while the slot is still held and must leave the queued connection parked;
#   4. the holder is closed and releases its slot; the next sweep reserves that slot and resumes
#      the queued connection, which is then closed and releases the slot it now owns;
#   5. a probe connection reserves the freed slot.
#
# Against the plugin before 508c1bea26 this aborts the server: the sweep resumed the queued
# connection in step 3 without a reservation, so the holder's close in step 4 landed the counter
# on zero and the resumed connection's close, releasing a slot it never held, wrapped the counter
# below zero; the probe's reserve() then tripped TSReleaseAssert(_active <= _limit). The test
# asserts no release wraps and no signal is logged, so it pins that fix.
#
# The connection is resumed and then closed, never closed while parked, because a client cannot
# close a parked connection as far as ATS is concerned: while the ClientHello hook is invoked ATS
# does not read the socket, so a FIN sits in the kernel until the sweep reenables the VC. The
# close-while-queued branch of sni_limiter.cc is therefore out of scope here and cannot be driven
# from a client; do not reintroduce a "closes while parked" step, it only ever resolved to the
# resume-then-close path this script now drives deterministically.
#
# Every step waits for the plugin's own debug line in traffic.out rather than sleeping for a
# guessed interval: on a loaded runner a fixed sleep let the second connection miss the holder
# entirely, so the queue path was silently never exercised (#13679). Every connection reads
# stdin from a FIFO that never delivers data, and is ended with kill -TERM on openssl's own PID.
# Neither the moment a connection opens nor the moment it closes is left to s_client: how it
# reacts to EOF on stdin differs between OpenSSL and LibreSSL, so a connection that must stay up
# is given stdin that never becomes readable, and one that must go is killed outright. The plugin
# only needs the VCONN_CLOSE that the resulting FIN produces.
#
# args: host port sni traffic_out
set -u
host="$1"
port="$2"
sni="$3"
traffic_out="$4"

OSSL="openssl s_client -connect ${host}:${port} -servername ${sni} -quiet"

# Ceiling per step. It only bounds a broken run; a healthy one moves on as soon as the line
# appears.
WAIT_LIMIT=30

holder=""
queued=""
probe=""

cleanup() {
  kill -TERM ${holder:-} ${queued:-} ${probe:-} 2>/dev/null || true
}
trap cleanup EXIT

# Shared stdin for every connection: a FIFO held open read-write on fd 3 that nothing ever
# writes to, so reads block and polls stay idle until the process is killed. Opening it
# read-write does not block, and the connections inherit fd 3 rather than opening the path,
# so the directory can go straight away.
fifo_dir="$(mktemp -d "${TMPDIR:-/tmp}/rl_holder.XXXXXX")"
mkfifo "${fifo_dir}/fifo"
exec 3<>"${fifo_dir}/fifo"
rm -rf "$fifo_dir"

# Poll traffic.out until the fixed string has appeared at least count times. Diags serialises
# each debug line and flushes it whole, so polling can never see a half-written line and a line
# is visible as soon as the plugin emits it. On timeout, fail loudly with what was expected and
# what the log holds so the sandbox is diagnosable.
wait_for() {
  needle="$1"
  count="$2"
  end=$((SECONDS + WAIT_LIMIT))
  while :; do
    seen=$(grep -c -F -- "$needle" "$traffic_out" 2>/dev/null || true)
    if [ "${seen:-0}" -ge "$count" ]; then
      return 0
    fi
    if [ "$SECONDS" -ge "$end" ]; then
      echo "timed out after ${WAIT_LIMIT}s waiting for occurrence ${count} of '${needle}' (saw ${seen:-0})" >&2
      echo "--- tail of ${traffic_out}:" >&2
      tail -n 20 "$traffic_out" >&2
      exit 1
    fi
    sleep 0.1
  done
}

# Close a connection by killing openssl itself. Escalate to KILL rather than waiting forever:
# a TERM-immune s_client would otherwise stall the run until autest's process timeout, turning a
# seconds-long failure into a ten-minute one.
#
# The whole body is redirected because the shell announces a signal-killed background job on the
# script's stderr as it reaps it ("Terminated: 15"), at whatever command happens to be running --
# redirecting the wait alone does not catch it. That noise would sit in stream.stderr.txt next to
# this script's real diagnostics, so keep it out and report an escalation on the saved fd instead.
end_connection() {
  exec 4>&2
  {
    kill -TERM "$1" 2>/dev/null || true
    waited=0
    while kill -0 "$1" 2>/dev/null; do
      if [ "$waited" -ge 30 ]; then
        echo "pid $1 ignored TERM after 3s; escalating to KILL" >&4
        kill -KILL "$1" 2>/dev/null || true
        break
      fi
      waited=$((waited + 1))
      sleep 0.1
    done
    wait "$1" || true
  } 2>/dev/null
  exec 4>&-
}

# 1. Holder: take the single slot.
${OSSL} <&3 >/dev/null 2>&1 &
holder=$!
wait_for 'Reserving a slot, active entities == 1' 1

# 2. Queued connection: the slot is full, so it is parked at the ClientHello hook.
${OSSL} <&3 >/dev/null 2>&1 &
queued=$!
wait_for 'Queueing the VC, we are at capacity' 1

# 3. Let the sweep (every 300ms) run while the slot is still held. This is a lower bound, not a
#    race: a correct sweep leaves the connection parked, which produces no line to wait for,
#    and a slower runner only gives it more sweeps.
sleep 1

# A resume here means the sweep dequeued while the limiter was still full, which is the bug this
# test pins. Only report it: the run must continue so the wrapped counter and the abort still
# reach the test's traffic.out assertions, but naming the first wrong event keeps it from being
# buried behind the probe's timeout 30s later.
resumed=$(grep -c -F -- 'Enabling queued VC' "$traffic_out" 2>/dev/null || true)
if [ "${resumed:-0}" -ne 0 ]; then
  echo "the sweep resumed the queued connection while the limiter was still full" >&2
fi

# 4. End the holder. The sweep then reserves the freed slot and resumes the queued connection;
#    ending that connection must release exactly the slot it was granted. The waits key on the
#    release line alone, not its value, so a wrapped counter still lets the probe run and trip
#    the assertion the test guards; the value is checked by the test's traffic.out testers.
end_connection "$holder"
wait_for 'Releasing a slot, active entities ==' 1
wait_for 'Enabling queued VC' 1
# The kill can land before ATS has acted on the reenable, or mid-handshake. Either way the read
# error closes the VC and the VCONN_CLOSE hook still fires, so the release below is not a race.
end_connection "$queued"
wait_for 'Releasing a slot, active entities ==' 2

# 5. Probe: reserve() must succeed against a balanced counter rather than tripping the
#    TSReleaseAssert(_active <= _limit) that a wrapped counter causes. This is the third
#    reservation: holder, sweep, probe.
${OSSL} <&3 >/dev/null 2>&1 &
probe=$!
wait_for 'Reserving a slot, active entities == 1' 3
end_connection "$probe"
wait_for 'Releasing a slot, active entities ==' 3

echo "rate_limit-queue-crash-done"
