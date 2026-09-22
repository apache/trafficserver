.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _developer-doc-net-timeouts:

Network Connection Timeouts
***************************

Every network connection can have an *inactivity* timeout (fire if nothing has
happened for N seconds) and an *active* timeout (fire N seconds after the
connection opened, regardless of activity). ``InactivityCop`` is the
continuation that dispatches both. One instance runs on each ``ET_NET`` thread,
scheduled every :ts:cv:`proxy.config.net.inactivity_check_frequency` seconds
(default 1).

Deadlines live on the ``NetEvent`` as absolute timestamps,
``next_inactivity_timeout_at`` and ``next_activity_timeout_at``, where 0 means
"no timeout". ``NetHandler::_earliest_deadline`` reduces the pair to the
single deadline the machinery below acts on: the non-zero minimum, or 0 if both
are 0.

The timer wheel
===============

``InactivityCop`` finds expired connections through a
``TimerWheel`` owned by the thread's ``NetHandler``
(``include/tscore/TimerWheel.h``). The wheel is a ring of intrusive lists —
buckets — one per second. Registering a connection puts it in the bucket for its
deadline; each tick drains only the buckets that have come due.

The cost of a tick is therefore proportional to the number of connections whose
deadline arrived, not to the number of connections open on the thread. That is
the whole point: the previous implementation rebuilt a work list by walking
*every* open connection once per second, which at 100,000 connections per thread
cost on the order of 10 ms per tick doing nothing useful.

The wheel holds ``N_BUCKETS - 1`` ticks of range — about 68 minutes at the
default 4096 buckets. A deadline beyond that is clamped into the furthest
reachable bucket and re-inserted when that bucket comes around, so a long
timeout costs one extra visit per ring traversal rather than being wrong. The
bucket count is a template parameter; 4096 was chosen by measurement, because at
every candidate size the common 30-second and 120-second timeouts cost exactly
one visit per timeout period, and only multi-hour timeouts (tunnel active
timeouts) care.

Lazy rearm, and the one rule that matters
=========================================

The wheel caches a copy of each element's deadline. **That cached deadline may be
earlier than the element's true deadline, but it must never be later**, or the
timeout fires late.

This asymmetry is what makes the design cheap:

*Extending* a deadline requires no wheel work at all. The cached value is merely
early; when the bucket comes due the wheel re-reads the real deadline, sees it is
still in the future, and relinks the element instead of firing it. This is
*lazy rearm*, and it is why ``UnixNetVConnection::netActivity`` — which runs
on every read and write — does no wheel bookkeeping in the common case.

*Setting, shortening, or closing* must re-arm, via
``NetEvent::rearm_timer``. Rather than reason about this site by site, every
place that writes a deadline calls ``rearm_timer()``, with one deliberate
exception noted below. If you add a new write site, call it.

Two consequences that are easy to get wrong
-------------------------------------------

**A closed connection must be reaped on the next tick, not at its deadline.**
``NetHandler::rearm_timer`` maps ``ne->closed`` to a deadline of *now*. The
old implementation swept every connection each second, so it noticed a closed
one within a tick; the wheel only visits what is scheduled, so without this a
closed connection whose deadline is minutes away would hold its file descriptor
until then.

**A connection with no deadline is not in the wheel at all.** If
``_earliest_deadline()`` is 0, ``rearm_timer()`` cancels rather than schedules.
So a connection that has no deadline yet cannot be "noticed later" by the cop the
way the old sweep would have noticed it — nothing will visit it until something
schedules it. This is the trap behind the default-timeout handling below.

The default inactivity timeout
==============================

:ts:cv:`proxy.config.net.default_inactivity_timeout` is a catch-all applied to
connections that have no explicit inactivity timeout of their own. It is stored
per-connection in ``default_inactivity_timeout_in``, seeded from the global in
``NetHandler::startCop`` and overridable per transaction.

The deadline it implies is armed at the moments its precondition becomes true:

- ``UnixNetVConnection::set_enabled``, when a VIO is first enabled and no
  explicit timeout is set;
- ``UnixNetVConnection::netActivity``, which re-arms from the default instead
  of clearing the deadline when there is no explicit timeout;
- ``UnixNetVConnection::cancel_inactivity_timeout``, which leaves the default
  active by design.

This matters because the *only* thing that used to apply the default was the
cop's own sweep, gated on a VIO being enabled. Under the wheel that does not
work: a connection with no deadline is never scheduled, so the cop never visits
it, so the default is never applied — the connection would never time out at
all. ``InactivityCop`` retains an equivalent arming block for any path that
enables I/O without going through ``set_enabled``, but it is a safety net, not
the mechanism.

``netActivity()`` is also the one place that deliberately skips
``rearm_timer()``, since extending is free — **except** on the 0-to-armed
transition. A connection whose default was 0 when ``set_enabled`` ran was never
scheduled, and extending a deadline it does not have would leave it out of the
wheel forever, so that single transition does re-arm. Everything after it is a
plain extension and stays free.

Budget
======

A tick's worth of due connections is normally small, but a correlated wave — an
origin outage, a load balancer draining — can make it large, and the callbacks
run inline on the poll thread. ``InactivityCop::TIMEOUT_BUDGET`` caps how
many timeouts one run will fire; the remainder stays in its bucket and is picked
up on the next tick.

Observability
=============

- :ts:stat:`proxy.process.net.inactivity_cop_visited` — connections the cop
  examined. This should track the number of connections coming due, **not** the
  number open. If it grows with total open connections, something is re-arming
  every tick and the wheel is being defeated.
- :ts:stat:`proxy.process.net.inactivity_cop_budget_exhausted` — runs that hit
  the budget. Sustained nonzero values mean timeouts on that thread are firing
  later than configured.
- :ts:stat:`proxy.process.net.inactivity_cop_lock_acquire_failure` — the cop
  could not lock a connection it needed to act on. Note this now only counts
  connections that were actually due; it previously counted an attempt on every
  connection every second.

Testing
=======

The wheel itself is a dependency-free container with unit tests in
``src/tscore/unit_tests/test_TimerWheel.cc`` — it needs no event system, so its
edge cases (lazy rearm, cancel, wraparound past the ring's range, budget
deferral, reentrant rescheduling from a fire callback) are tested directly.

``InactivityCop`` has a microbenchmark at
``src/iocore/net/unit_tests/benchmark_InactivityCop.cc`` that drives the real cop
against mock ``NetEvent`` objects at up to 100,000 connections. It is tagged
``[!benchmark]`` so ``ctest`` skips it; run it with:

.. code-block:: bash

   ./test_net '[inactivity_cop]'

End-to-end behavior is covered by the autests in ``tests/gold_tests/timeout/``.
Those are the real gate for anything in this document — in particular
``default_inactivity_timeout.test.py`` for the arming rules above. They need a
Linux environment that permits socket binds.
