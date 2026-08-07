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

Metrics
*******

Synopsis
========

.. code-block:: cpp

    #include "tsutil/Metrics.h"

``ts::Metrics`` is the metrics registry. A metric is a named ``int64_t`` counter or gauge,
reached either by an integer id or by a pointer to its underlying atomic. This page covers two
facilities layered on top of it: a separate store for metrics that should not be published, and
derived metrics that aggregate other metrics.

Metric types
============

Every metric has a ``ts::Metrics::MetricType``, either ``COUNTER`` (monotonically increasing)
or ``GAUGE`` (rises and falls). The type is chosen by the facade used to create the metric,
``ts::Metrics::Counter`` or ``ts::Metrics::Gauge``, and is encoded into the metric id.

.. code-block:: cpp

    auto *hits = ts::Metrics::Counter::createPtr("proxy.process.example.hits");
    auto *live = ts::Metrics::Gauge::createPtr("proxy.process.example.live");

    ts::Metrics::Counter::increment(hits);
    ts::Metrics::Gauge::store(live, 5);

The two stores
==============

There are two entirely separate stores:

``ts::Metrics::instance()``
   The published store. Everything here is visible to :program:`traffic_ctl`, the JSONRPC API and
   ``stats_over_http``.

``ts::Metrics::hidden_instance()``
   The hidden store. Metrics here are recorded normally but are never published.

Hidden metrics exist for high cardinality intermediate values, where the individual values are not
useful to publish but an aggregate over them is. A separate store is used rather than a
"hidden" flag on each metric so that hidden metrics are *structurally* unreachable from the
published store: no consumer can expose one by forgetting to check a flag.

Create a hidden metric with ``createHiddenPtr`` on either facade:

.. code-block:: cpp

    auto *g = ts::Metrics::Gauge::createHiddenPtr("proxy.process.example.per_thing.", thing_name);

    // The ordinary typed mutators work unchanged on a hidden metric.
    ts::Metrics::Gauge::increment(g);
    ts::Metrics::Gauge::decrement(g);

``createHiddenPtr`` returns the same correctly typed pointer as ``createPtr``, so a hidden metric is
read and written with the normal mutators and no cast is needed at the call site. There are two
overloads on each facade, one taking a name and one taking a prefix and a name.

.. important::

   An id from one store is meaningless in the other. Both stores number their metrics from zero, so
   passing a hidden id to the published store silently reads a different metric, with no error and
   no crash. Prefer ``createHiddenPtr``, which returns a pointer and never hands out an id.

Inspecting hidden metrics
-------------------------

Because hidden metrics are invisible to normal queries, they can be listed explicitly with
``traffic_ctl metric match --include-hidden``. This sets an additional record type bit which
is deliberately outside ``RECT_ALL``, so hidden metrics are returned only when asked for by name and
never as a side effect of a broad query.

.. note::

   Hidden metrics are internal. They are not part of the stable metric contract and may be added,
   renamed or removed between releases without notice. Do not build monitoring on them; use the
   published aggregate instead.

Derived metrics
===============

A derived metric is a published metric whose value is computed from other metrics, its *sources*. A
source may live in either store, which is the point of the facility: high cardinality sources stay
hidden while only the aggregate is published.

Sources are combined with one of three operations, ``ts::Metrics::Derived::Op``:

``SUM``
   Add the sources together. This is the default.

``MAX``
   The largest source value.

``MIN``
   The smallest source value.

Declaring aggregates up front
-----------------------------

``ts::Metrics::Derived::derive()`` takes a list of specifications and is meant for aggregates whose
sources are all known at startup. Each source may be given as a pointer, an id or a name:

.. code-block:: cpp

    ts::Metrics::Derived::derive({
      {"proxy.process.example.total", ts::Metrics::MetricType::COUNTER, {a, b, c}},
      {"proxy.process.example.peak",  ts::Metrics::MetricType::GAUGE,   {a, b, c},
        ts::Metrics::Derived::Op::MAX},
    });

A source that does not resolve, because the name or id is unknown, is skipped.

Building aggregates at runtime
------------------------------

``ts::Metrics::Derived::add_source()`` adds a single source to a derived metric, creating the
derived metric if it does not exist yet. Use it when sources are discovered as the process runs, for
example one per upstream server as traffic arrives:

.. code-block:: cpp

    ts::Metrics::Derived::add_source("proxy.process.example.total", ts::Metrics::MetricType::COUNTER,
                                     per_thing_metric);

Repeatedly calling ``ts::Metrics::Derived::derive()`` for the same derived name does **not** work
for this: each call appends a separate entry targeting the same metric, so every update overwrites
the others with its own subset of sources and the last one to run silently wins.
``ts::Metrics::Derived::add_source()`` accumulates into a single entry instead.

Adding a source that is already registered for that derived metric is a no-op, so a caller which may
re-register the same source, such as one recreating an object for the same key, need not track that
itself. The ``type`` and ``op`` arguments are ignored if the derived metric already exists.

A hidden source can feed a published aggregate:

.. code-block:: cpp

    auto *hidden = ts::Metrics::Gauge::createHiddenPtr("per_thing.", name);

    ts::Metrics::Derived::add_source("proxy.process.example.live", ts::Metrics::MetricType::GAUGE,
                                     hidden, ts::Metrics::Derived::Op::SUM);

When derived values update
--------------------------

Derived metrics are not recomputed when a source changes. They are recalculated by
``ts::Metrics::Derived::update_derived()``, which runs on an ``ET_TASK`` thread every
``REC_RAW_STAT_SYNC_INTERVAL_MS``, currently 5000 ms. Consequences:

* A derived value lags its sources by up to one interval.
* Reading a derived metric immediately after changing a source returns the previous value. Unit
  tests must call ``ts::Metrics::Derived::update_derived()`` directly.
* The cost of the pass is proportional to the total number of registered sources, and it runs
  single threaded while holding a lock. Registering very large numbers of sources is therefore not
  free, even though registration itself is rare.

Because the pass samples its sources, a derived ``MAX`` reports the largest value *observed at a
sampling point*, not the true peak. There are two ways to arrange this, with different tradeoffs:

* A ``MAX`` over instantaneous gauges is sampled, so a brief spike occurring between two samples is
  not observed. The value rises and falls with the sources, so a monitoring system that scrapes it
  can compute a maximum over any time window.
* A ``MAX`` over monotonically increasing sources, such as each source's own all-time peak, is exact
  and never misses a spike. It also never decreases, so the time dimension is lost: the value
  reports only that a peak occurred at some point, not when.

Which is appropriate depends on whether the consumer needs to aggregate over time downstream.

Storage limits
==============

Metrics are allocated from fixed size blobs, ``MAX_BLOBS`` of ``MAX_SIZE`` entries each, for a
maximum of about 8M metrics per store. Creating a metric when the store is full returns the reserved
``bad_id`` rather than growing past the end, so an exhausted store degrades to writing into a
throwaway slot instead of corrupting memory. Reaching this limit means the naming scheme is
unbounded, and hidden metrics with per-connection or per-URL names are the likely cause.
