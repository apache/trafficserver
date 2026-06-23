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
.. default-domain:: cpp

.. _cache-shm-fast-restart:

Shared-Memory Cache Directory (Fast Restart)
********************************************

.. note::

   This is an experimental feature, disabled by default. It is controlled by
   the ``proxy.config.cache.shm.*`` settings (see :ref:`configuration
   <cache-shm-configuration>`). The administrator-facing description lives at
   :ref:`admin-cache-shm-fast-restart`; this document covers the design.

Motivation
==========

The :ref:`cache directory <cache-directory>` is the memory-resident index that
maps cached objects to their location on disk. It is rebuilt every time |TS|
starts: each stripe reads its two on-disk directory copies, picks the newer
valid one, and then runs recovery (``StripeSM::recover_data``) to replay
the fragments written since the last directory sync. For a large cache this is
the dominant cost of a restart -- the cache is not online, and therefore not
serving from cache, until it finishes.

The directory itself, however, is purely a function of state |TS| already had
in memory in the previous process. If that memory could *survive* the process
restart, the new process could attach it and come online immediately, skipping
both the disk read and recovery.

The shared-memory fast-restart feature does exactly that. It hosts each
stripe's ``Directory::raw_dir`` buffer in a POSIX shared-memory segment
(:manpage:`shm_open(3)`, on Linux backed by ``tmpfs`` under ``/dev/shm``).
Because the segment is owned by the kernel and not by the process, it outlives
an orderly ``traffic_server`` exit. The next start re-maps the existing segment
in milliseconds instead of rebuilding from disk.

Design principles
=================

The feature is built around two non-negotiable invariants.

**The on-disk cache is always the source of truth.** The shared-memory
directory is *only* an optimization of restart time. The data fragments
themselves are never kept in shared memory -- they are read from disk on demand
exactly as before. The shared segment holds the directory index and nothing
else.

**Recovery is binary.** The shared segment is either trustworthy enough to
attach wholesale, or it is dropped and the stripe rebuilds from disk through
the existing cold-start path. There is no attempt to repair, partially trust,
checksum, or torn-write-detect the segment. Every gate described below is a
fail-closed test: if anything is wrong or even ambiguous, the answer is "drop
and rebuild," which is always correct because the disk is authoritative.

This keeps the trusted code small. The fast path adds no new durability
mechanism; it borrows the one the cache already has. Whenever the shared
segment is unavailable for any reason, |TS| takes precisely the path it takes
today after an unclean shutdown.

Object layout
=============

The feature uses two kinds of shared-memory object, defined in
:ts:git:`src/iocore/cache/CacheShmLayout.h`.

.. code-block:: text

   POSIX shared memory  (e.g. /dev/shm on Linux)

   <prefix>control                              one per traffic_server instance
   +-------------------------------------------------------------+
   |  magic "ATS-SHM\0"   schema_version   abi_hash              |
   |  storage_signature   clean_shutdown   owner_pid             |
   |  stripe_count                                               |
   |  stripes[0 .. MAX_STRIPES-1]:                               |
   |       { shm_name, raw_dir_size, stripe_key_hash }           |
   +-------------------------------------------------------------+
         |                  |                  |
         v                  v                  v
   <prefix>s0           <prefix>s1         <prefix>s2     per-stripe raw_dir
   +-----------+        +-----------+      +-----------+
   |  header   |        |  header   |      |  header   |  StripeHeaderFooter
   |  dir[]    |        |  dir[]    |      |  dir[]    |  directory entries
   |  footer   |        |  footer   |      |  footer   |
   +-----------+        +-----------+      +-----------+

The control segment
-------------------

There is one control segment per instance, named ``<prefix>control``. It is a
fixed-size ``cache_shm::CacheShmControl`` -- a header plus a table of
up to ``MAX_STRIPES`` (256) ``cache_shm::StripeEntry`` rows. A
``static_assert`` keeps the whole control segment under 32 KiB. Its fields:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Field
     - Purpose
   * - ``magic``
     - ``"ATS-SHM\0"``. Identifies a |TS| control segment and is the first
       thing checked on attach.
   * - ``schema_version``
     - The on-shm wire-format version. Bumped whenever the meaning of the
       layout changes; a mismatch drops the segment.
   * - ``abi_hash``
     - A compile-time fingerprint of the binary's directory structures (see
       ``CacheShm::abi_hash``). A mismatch -- e.g. after an upgrade that
       changed ``Dir`` -- drops the segment.
   * - ``storage_signature``
     - A fingerprint of the ``storage.config`` topology. **Not** a hard
       gate; see `Storage changes and partial attach`_.
   * - ``clean_shutdown``
     - ``1`` only between a clean shutdown and the next attach. ``0`` at all
       other times, including throughout a running process, so a crash leaves
       it ``0``.
   * - ``owner_pid``
     - PID of the process currently mapping the segment read-write, or ``0``
       when none. Backs the concurrent-attach guard.
   * - ``stripe_count``
     - High-water mark of used rows in ``stripes[]``.
   * - ``stripes[]``
     - One row per stripe: its segment name, the segment's byte size, and the
       64-bit stripe identity hash used to match a stripe to its prior segment.

Per-stripe directory segments
-----------------------------

Each stripe's directory lives in its own segment, ``<prefix>s<N>``. The mapped
region *is* the stripe's ``Directory::raw_dir``: the
:cpp:class:`StripeHeaderFooter` header, the array of :cpp:class:`Dir` entries,
and the footer, in exactly the same byte layout the cache writes to disk. A
stripe reads and writes its directory through this mapping for the entire run,
so the segment is continuously current -- there is no separate "flush to shared
memory" step.

Naming
------

All names derive from :ts:cv:`proxy.config.cache.shm.name_prefix`, which is just
the middle word (default ``ats``). |TS| frames that word as ``/<word>-`` -- the
leading ``/`` that POSIX shared memory requires and the trailing ``-`` separator
are supplied by ``cache_shm::normalize_name_prefix``, not the operator,
so neither can be mis-typed; any stray framing carried over from an older config
(for example a literal ``/ats-``) is trimmed first, so it can never become an
invalid embedded-slash name like ``//ats--``. With the default word the framed
prefix is ``/ats-``: the control segment is ``/ats-control`` and stripe segments
are ``/ats-s<N>`` where ``N`` is a per-instance slot index. Names are kept under
``cache_shm::MAX_SHM_NAME_LEN`` (31) characters because macOS caps POSIX
shared-memory names (``PSHMNAMLEN``) at 31 including the leading ``/``; keeping
to that limit makes the same naming work on Linux and macOS. Instances sharing
a host **must** use distinct words so their segments do not collide.

Note that the stripe segment name is just a slot label. A stripe is matched to
its prior segment by ``stripe_key_hash`` (a 64-bit FNV-1a of the stripe's
``hash_text``), **not** by name or index, so a span going offline can shift
slot numbers without breaking the identity match.

Startup
=======

``CacheShm::initialize`` runs from
``CacheProcessor::start_internal``, after the :cpp:class:`Store` is
read but before any :cpp:class:`Stripe` is constructed. It loads the
configuration, then opens the control segment and selects one of three modes:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Mode
     - Meaning
   * - ``Disabled``
     - The feature is off (or a fatal precondition failed, such as a name that
       is too long or losing the concurrent-attach race). Stripes use the
       normal heap/hugepage directory; behavior is identical to stock |TS|.
   * - ``AttachExisting``
     - A trustworthy prior control segment exists. Stripes attach their prior
       segment by identity, or create a fresh one where there is no match.
   * - ``CreateFresh``
     - No usable prior control segment. A new one is created and every stripe
       segment is created empty (the cold path, but now shared-memory-backed
       for *next* time).

Trust gates
-----------

When a prior control segment exists, ``initialize`` applies these gates in
order. The first failure drops the entire control segment (unlinking every
stripe segment it lists) and falls through to ``CreateFresh``:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Gate
     - Drops the segment when...
   * - concurrent-attach guard
     - another live process is mapping the segment (see below). This actually
       disables shared memory for the run rather than dropping -- the live
       owner's segment must be left intact.
   * - ``magic``
     - the magic bytes do not match (not our segment, or corrupt).
   * - ``schema_version``
     - the on-shm format version differs from this binary's.
   * - ``abi_hash``
     - the binary's directory structures differ from the writer's (e.g. an
       upgrade changed ``Dir``, ``StripeHeaderFooter``, ``DIR_DEPTH``, ...).
   * - ``clean_shutdown``
     - the previous run did not set it to ``1`` -- i.e. it crashed or was
       killed. A crash may have left directory entries pointing at fragments
       that were never flushed, so no stripe can safely skip recovery.

If every gate passes, ``initialize`` adopts the segment: it records itself as
``owner_pid``, sets ``clean_shutdown = 0`` (so a crash *this* run drops the
segment next time), ``msync``\ s the header, and enters ``AttachExisting``. The
per-stripe work then happens lazily as each stripe initializes.

Concurrent-attach guard
-----------------------

Two ``traffic_server`` processes must never map the same directory read-write;
the second would corrupt the first's live index. ``clean_shutdown`` is no help
here -- it says nothing about a process that is *currently* running. The guard
is therefore based on ownership, with two layers:

* **flock.** ``initialize`` takes a non-blocking exclusive ``flock`` on the
  control-segment fd and holds it for the entire process lifetime
  (``g_control_fd``). The kernel releases it automatically on exit *or crash*,
  so it is self-healing. If the lock is already held
  (``LockResult::HeldByOther``), a live owner exists and the new process
  disables shared memory for its run. This is authoritative on Linux/``tmpfs``.

* **owner_pid liveness.** macOS POSIX shared memory does not honor ``flock``
  (``LockResult::Unsupported``). There, the guard falls back to the recorded
  ``owner_pid``: if it names a live process other than ourselves
  (``CacheShm::process_is_alive``, via ``kill(pid, 0)``), the new
  process disables shared memory. A clean shutdown clears ``owner_pid`` to
  ``0``; a crash leaves a stale pid, but a crash also leaves
  ``clean_shutdown = 0``, so the segment is dropped by that gate anyway.

A symmetric check guards the ``CreateFresh`` path: after creating the fresh
control segment, ``initialize`` takes the lock, and if it lost a creation race
to another starting process it backs out and disables shared memory for the
run.

Per-stripe attach and the fast path
====================================

For each stripe, ``Stripe::_init_directory`` asks
``CacheShm::attach_or_create_stripe`` for its ``raw_dir`` *before*
falling back to the hugepage / aligned-heap allocation:

.. code-block:: cpp

   this->directory.raw_dir = CacheShm::attach_or_create_stripe(hash_text.get(), directory_size);
   if (this->directory.raw_dir == nullptr) {
     // shm disabled or attach/create failed -> hugepage, then aligned heap
   }

``attach_or_create_stripe`` looks up the stripe by ``stripe_key_hash`` in the
control table:

* **Match found** (and the recorded size matches): map the existing segment and
  return it. This is the segment the previous run left behind.
* **No match**: reserve a fresh table slot and create a new, zero-filled
  segment.

A freshly created segment has a zero header magic, so the fast-attach gate
below rejects it and ``StripeSM::init`` falls through to the normal disk
read, which repopulates the directory in place.

The fast-attach gate
--------------------

In ``AttachExisting`` mode, when ``raw_dir`` came from shared memory,
``StripeSM::init`` checks whether the in-segment directory can be trusted
without reading disk:

#. ``header->magic`` and ``footer->magic`` are both ``STRIPE_MAGIC``;
#. the directory version is within
   ``[CACHE_DB_MAJOR_VERSION_COMPATIBLE, CACHE_DB_MAJOR_VERSION]``;
#. ``Stripe::_shm_directory_is_valid`` passes (see below).

When all three hold, the stripe skips both the disk read **and**
``StripeSM::recover_data`` -- which would otherwise rescan the tail and
discard the very entries the shared segment preserved -- and jumps straight to
the post-recovery state (``sector_size``, ``scan_pos``,
``periodic_scan``, then ``StripeSM::dir_init_done``),
mirroring the tail of ``handle_recover_write_dir()``. It logs::

   attaching cached directory from shm for '<stripe>' (fast restart, recovery skipped)

If any check fails, it logs ``shm directory invalid ...; falling back to disk
read`` and proceeds exactly as a cold start would.

Bounds-validating a trusted segment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The magic/version checks confirm the segment *looks like* a directory, but a
stale-yet-magic-valid segment could still present offsets that would turn into
out-of-bounds disk I/O. ``Stripe::_shm_directory_is_valid`` is a
defensive bounds check over the trusted header fields:

* ``sector_size`` is non-zero and no larger than ``STORE_BLOCK_SIZE``;
* ``write_pos``, ``last_write_pos`` and ``agg_pos`` all lie within the stripe's
  data region (``[start, skip + len]``);
* every per-segment free-list head indexes a ``Dir`` entry within its segment
  (walking a free list from an out-of-range head would run off the end).

A failure here is treated like any other attach miss: drop to the disk read and
recover. It is cheap insurance against a corrupted or version-skewed segment
that slipped past the coarse magic check.

Storage changes and partial attach
===================================

A change to ``storage.config`` does **not** invalidate the whole control
segment. ``storage_signature`` is recorded and used only to phrase the startup
log line ("partial -- storage changed"); it is not a trust gate. The actual
reconciliation is per stripe, driven by identity:

* A stripe whose ``stripe_key_hash`` still matches a table entry of the right
  size attaches its prior segment as usual.
* A stripe that is new, relocated, or resized finds no match and creates a
  fresh segment (then loads from disk).
* A table entry that *no* stripe claimed this run is an **orphan** -- its stripe
  left the cache (a span was dropped, or a disk failed to open).

``CacheShm::finalize_attach``, called from
``CacheProcessor::cacheInitialized`` once every stripe has initialized,
reclaims the orphans: it unlinks each unclaimed segment, tombstones its slot for
reuse, and trims trailing tombstones so ``stripe_count`` tracks the live
high-water mark.

One guard matters here: if **zero** stripes claimed a segment this run,
``finalize_attach`` leaves every entry intact. Zero claims cannot be
distinguished from an aborted init (for example a transient ``volume.config``
error), and reclaiming a valid cache's segments would be far worse than leaking
them for one run.

Shutdown
========

A clean shutdown is what makes the next start fast, so the directory must be
made final and the segment marked clean -- in that order.

Wiring
------

On a clean exit, ``AutoStopCont::mainEvent`` calls
``sync_cache_dir_on_shutdown()`` whenever the cache is initialized.
``sync_cache_dir_on_shutdown`` stops every stripe (taking
each stripe mutex, so all writers are quiesced), and only then calls
``CacheShm::mark_clean_shutdown``, which sets ``clean_shutdown = 1``,
clears ``owner_pid`` to ``0``, and ``msync``\ s the header. When the feature is
disabled, ``mark_clean_shutdown`` is a no-op (there is no control segment), so
the shutdown path is unchanged for a stock |TS|.

Skipping the on-disk directory write
------------------------------------

For a shared-memory-backed stripe, ``StripeSM::shutdown`` still flushes
the aggregation buffer (so pending *content* reaches disk) but then **skips the
on-disk directory write** entirely:

.. code-block:: text

   Dir <stripe>: shm-backed, skipping on-disk directory write

The shared segment is already the durable copy of the directory and is attached
directly next start, so writing the A/B copies to disk would be pure waste. The
trade-off is deliberate and safe: if the segment is later dropped for any
reason, the on-disk A/B copies plus ``StripeSM::recover_data`` reconcile
the tail -- the same path an unclean restart already takes.

Invalidating on flush failure
-----------------------------

If the aggregation-buffer flush at shutdown fails (e.g. the disk went bad), the
on-disk content no longer matches the directory, so the shared segment must not
be trusted next start. ``StripeSM::shutdown`` calls
``CacheShm::invalidate_stripe_directory``, which zeroes the in-segment
header magic and ``msync``\ s it. Next start, the fast-attach gate rejects that
one stripe on the magic check and it reloads from disk and recovers, while the
other stripes still fast-attach.

Crash and recovery summary
==========================

The state machine reduces to: *the segment is attached only when it is provably
consistent, and dropped otherwise.*

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Event between runs
     - Next start
   * - Clean shutdown, unchanged binary & storage
     - Fast attach. Recovery skipped. Cache online in milliseconds.
   * - Crash / ``SIGKILL``
     - ``clean_shutdown`` still ``0`` -> drop, rebuild from disk + recover.
   * - Binary upgrade changing directory structures
     - ``abi_hash`` mismatch -> drop, rebuild.
   * - Schema bump
     - ``schema_version`` mismatch -> drop, rebuild.
   * - ``storage.config`` change
     - Control segment kept; matching stripes fast-attach, changed stripes
       rebuild, orphans reclaimed.
   * - Per-stripe shutdown flush failed
     - That stripe's segment was invalidated -> it rebuilds; others
       fast-attach.
   * - Another live owner using the prefix
     - Refuse to attach; shared memory disabled for this run.

In every "drop/rebuild" row, |TS| behaves exactly as it does today without the
feature -- the fast path is the only thing lost.

Huge pages
==========

The large directory segments make page-table teardown at process exit
non-trivial: ``exit_mmap`` walks O(number of PTEs), which for multi-gigabyte
directories can cost seconds. Backing the mapping with huge pages cuts the PTE
count ~512x and the teardown cost with it.

When :ts:cv:`proxy.config.cache.shm.use_hugepages` is set, |TS| advises
transparent huge pages on the mapping with ``madvise(MADV_HUGEPAGE)``.
``MAP_HUGETLB`` is deliberately **not** used: ``shm_open`` fds are ``tmpfs``
backed, and ``MAP_HUGETLB`` requires a ``hugetlbfs`` fd, so it always fails with
``EINVAL``. The advice requires shmem THP to be enabled on the host (for
example ``/sys/kernel/mm/transparent_hugepage/shmem_enabled`` set to ``advise``
or ``always``, or the ``tmpfs`` mounted with ``huge=advise``). When huge pages
are unavailable the ``madvise`` simply logs a debug line under the
``cache_shm`` tag and the kernel uses base pages, so enabling the setting is
always safe.

Concurrency model
=================

Stripes initialize concurrently across the AIO/disk threads, so the
control-table bookkeeping is locked, but the slow shared-memory syscalls are
kept out of the critical section:

* ``g_table_mutex`` guards the control-segment stripe table and the per-run
  claim bookkeeping. ``attach_or_create_stripe`` decides what to do (reuse a
  table slot or reserve a fresh one) under the lock, then **drops it** before
  ``shm_open`` / ``ftruncate`` / ``mmap``. Each stripe owns a distinct segment,
  so the syscalls never touch another thread's segment. Holding the lock across
  them would serialize every disk thread's init.
* ``g_pointers_mutex`` guards the set of pointers handed out, so
  ``CacheShm::is_shm_pointer`` (used to tell a shm-backed directory from
  a heap-allocated one, e.g. to skip the redundant on-disk directory write) is
  thread-safe.
* Slot reservation tombstones a slot if the create later fails
  (``release_reserved_slot``), so a failed create cannot strand a half-built
  table entry.

Disabling the feature: stale-segment purge
==========================================

Running with the feature **disabled** after it had been enabled is hazardous in
two ways: the leftover segments keep consuming memory the disabled instance
never reads, and a later re-enabled run could fast-attach a directory that went
stale while |TS| ran disabled (writing only to disk). To address this,
:ts:cv:`proxy.config.cache.shm.purge_stale_on_start` (opt-in) makes a disabled
start best-effort remove any leftover segments for the configured prefix.

The purge shares one primitive with the operator tooling (see below):
``cache_shm::purge_segments`` in :ts:git:`src/iocore/cache/CacheShmPurge.h`. It
enumerates the stripe table and unlinks every stripe segment plus the control
object, returning a structured ``PurgeReport`` that each caller renders in its
own format. It refuses to unlink anything owned by a live process (the same
flock + ``owner_pid`` guard used at attach), and it never blocks startup. An
already-gone segment (``ENOENT``) is the desired end state and is not counted as
a failure.

Operator tooling: ``traffic_ctl cache shm``
============================================

Because crash-leftover segments may need inspecting when no live process is
around to query, the tooling acts on the shared-memory objects **directly**, via
``shm_open``, rather than over JSON-RPC. For that reason ``traffic_ctl`` does
**not** link the cache library; the small amount of shared logic lives in
header-only form (:ts:git:`src/iocore/cache/CacheShmLayout.h` and
``CacheShmPurge.h``).

``traffic_ctl cache shm status [--prefix P]``
   Maps the control segment read-only and prints its header (magic,
   schema/abi/storage fingerprints, ``clean_shutdown``, and whether
   ``owner_pid`` names a live process) followed by the stripe table, flagging
   each segment ``present`` / ``MISSING`` and each free slot as a tombstone.

``traffic_ctl cache shm clear [--prefix P]``
   Removes the segments via the shared ``purge_segments`` primitive. It
   **refuses** to clear segments owned by a live ``traffic_server`` (stop it
   first), so it cannot orphan a running instance's fast restart. This is the
   on-demand equivalent of ``purge_stale_on_start``.

.. _cache-shm-configuration:

Configuration
=============

All settings are under ``proxy.config.cache.shm`` and take effect only on a
restart (``RECU_RESTART_TS``). See :ref:`admin-cache-shm-fast-restart` for the
full administrator-facing descriptions.

.. list-table::
   :header-rows: 1
   :widths: 38 12 50

   * - Setting
     - Default
     - Effect
   * - :ts:cv:`proxy.config.cache.shm.enabled`
     - ``0``
     - Master switch. ``0`` = always read the directory from disk (stock
       behavior).
   * - :ts:cv:`proxy.config.cache.shm.name_prefix`
     - ``ats``
     - Middle word of the shared-memory object names; framed as ``/<word>-``
       (the ``/`` and ``-`` are added by |TS|). Give co-located instances
       distinct words.
   * - :ts:cv:`proxy.config.cache.shm.use_hugepages`
     - ``0``
     - Advise transparent huge pages on the directory mappings. Safe when
       unavailable; falls back to base pages.
   * - :ts:cv:`proxy.config.cache.shm.purge_stale_on_start`
     - ``0``
     - When the feature is disabled, best-effort remove leftover segments for
       the prefix at startup.

Platform considerations
=======================

* **Linux** is the primary target: ``tmpfs`` (``/dev/shm``) backs the segments,
  ``flock`` is authoritative for the concurrent-attach guard, and shmem THP
  provides the huge-page teardown win.
* **macOS** is supported for development and testing on a best-effort basis.
  POSIX shared-memory names are limited to 31 characters (the reason for
  ``MAX_SHM_NAME_LEN``), ``flock`` is not honored on shm fds, so the
  concurrent-attach guard is best-effort there: it relies on the ``owner_pid``
  liveness backstop alone (the ``kill(pid, 0)`` check), which closes the window
  but cannot make the attach atomic the way ``flock`` does on Linux. The kernel
  also rounds a segment up to a page boundary, so ``open_and_map_shm`` accepts
  any size in ``[requested, page-up]``.
* The feature is inert at the default :ts:cv:`proxy.config.cache.shm.enabled`
  ``0``: no segments are created or attached on any platform, and behavior is
  identical to stock |TS|.
* Realistic multi-gigabyte directory sizes, the ``MADV_HUGEPAGE`` teardown win,
  and the restart-time benchmarks are Linux-only -- the same platform boundary
  |TS| already has for its hugepage directory allocation. (Recall ``MAP_HUGETLB``
  is never used here; see `Huge pages`_.)

Testing
=======

The pure trust-gate logic is unit-tested in
:ts:git:`src/iocore/cache/unit_tests/test_CacheShm.cc` (ABI-hash stability, the
storage-signature topology sensitivity, control-header round-trip, the macOS
name-length limit, and the process-liveness check).

The end-to-end behavior is covered by autests in
:ts:git:`tests/gold_tests/cache/`, one scenario each:

.. list-table::
   :header-rows: 1
   :widths: 42 58

   * - Test
     - Scenario
   * - ``cache_shm_fast_restart``
     - Directory survives a clean shutdown and is fast-attached.
   * - ``cache_shm_data_integrity``
     - Objects cached before shutdown are served byte-identical from cache
       after the attach (including multi-fragment objects).
   * - ``cache_shm_unclean_shutdown``
     - ``SIGKILL`` leaves the segment dirty; next start drops and rebuilds.
   * - ``cache_shm_schema_mismatch``
     - A poked ``schema_version`` is dropped, never attached.
   * - ``cache_shm_storage_mismatch``
     - A changed storage layout keeps the control segment, creates a fresh
       relocated stripe, and reclaims the orphan.
   * - ``cache_shm_bad_disk_dropped``
     - Dropping a disk fast-attaches the survivors and reclaims the removed
       disk's segment.
   * - ``cache_shm_concurrent_attach``
     - A second ``traffic_server`` refuses to attach over a live owner and runs
       with shared memory disabled.
   * - ``cache_shm_purge_on_disable``
     - ``purge_stale_on_start`` removes leftover segments on a disabled start.

The schema/storage tests drive their gates by editing ``/dev/shm`` directly
(``shm_poke.py``), which is a Linux facility; they have no macOS condition.

Limitations and non-goals
=========================

* The feature accelerates restart only; it does not change steady-state cache
  behavior, durability, or the on-disk format.
* Only the directory is shared, never cached content.
* There is no migration or repair of an untrusted segment -- the disk is
  authoritative and rebuilding from it is always the fallback.
* A single host may run multiple instances only with distinct
  ``name_prefix`` values.

Source map
==========

.. list-table::
   :header-rows: 1
   :widths: 42 58

   * - File
     - Role
   * - :ts:git:`src/iocore/cache/CacheShm.h` / ``CacheShm.cc``
     - The ``CacheShm`` facade: initialize, attach/create, finalize, mark-clean,
       invalidate, and the trust-gate fingerprints.
   * - :ts:git:`src/iocore/cache/CacheShmLayout.h`
     - The on-shm control-segment layout, shared with tooling.
   * - :ts:git:`src/iocore/cache/CacheShmPurge.h`
     - The header-only enumerate-and-unlink primitive and its owner guard,
       shared by the disabled-start purge and ``traffic_ctl``.
   * - :ts:git:`src/iocore/cache/Stripe.cc`
     - The shared-memory ``raw_dir`` allocation and ``_shm_directory_is_valid``.
   * - :ts:git:`src/iocore/cache/StripeSM.cc`
     - The fast-attach gate in ``StripeSM::init`` and the shutdown-write skip /
       invalidate in ``StripeSM::shutdown``.
   * - :ts:git:`src/iocore/cache/CacheProcessor.cc`
     - ``initialize`` / ``finalize_attach`` call sites in ``CacheProcessor``.
   * - :ts:git:`src/iocore/cache/CacheDir.cc`
     - ``mark_clean_shutdown`` from ``sync_cache_dir_on_shutdown``.
   * - :ts:git:`src/traffic_ctl/CacheShmCommand.cc`
     - The ``traffic_ctl cache shm status`` / ``clear`` commands.
