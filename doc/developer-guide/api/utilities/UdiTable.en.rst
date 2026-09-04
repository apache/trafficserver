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

.. include:: ../../../common.defs

.. _udi-table:

UdiTable
********

``ts::UdiTable`` is a fixed-size table for tracking frequently observed keys
without allowing memory use to grow with the key space. Each key has a score
and an associated user-defined data object. Events for a tracked key increase
its score. A new key competes with a slot selected by a rotating pointer; a
losing contest weakens the selected entry, while a winning contest replaces
it. Frequently observed keys therefore tend to remain in the table.

The implementation serializes table operations with one mutex. Data objects
are held by ``std::shared_ptr``, so a pointer returned by ``find()`` or
``process_event()`` remains valid after its table entry is removed or evicted.

Basic Use
=========

Include ``tsutil/UdiTable.h`` and supply a default-constructible key and data
type. The key must also support the selected hash and equality operations.

.. code-block:: cpp

   #include "tsutil/UdiTable.h"

   #include <atomic>
   #include <string>

   struct KeyData {
     std::atomic<uint64_t> requests{0};
   };

   ts::UdiTable<std::string, KeyData> table(10'000);

   if (auto data = table.process_event("client-id")) {
     data->requests.fetch_add(1, std::memory_order_relaxed);
   }

``process_event()`` returns an empty pointer when a new key loses its contest.
The optional ``ProcessStatus`` output distinguishes an ordinary contest loss
from a contest that could not find an eligible slot.

Eviction Policies
=================

By default every occupied slot may be selected for replacement. A fourth
template argument can provide a policy that temporarily protects entries. The
predicate receives the stored data as a const reference and returns whether
the slot may participate in a contest.

.. code-block:: cpp

   struct Data {
     std::atomic<bool> protected_entry{false};
   };

   struct CanEvict {
     bool operator()(Data const &data) const
     {
       return !data.protected_entry.load(std::memory_order_relaxed);
     }
   };

   ts::UdiTable<std::string, Data, std::hash<std::string>, CanEvict> table(1024);

The predicate runs while the table mutex is held. It should be inexpensive
and must not reenter the same table. A contest examines at most 1024 slots so
a table of protected entries cannot cause an unbounded scan. If no eligible
slot is found, ``process_event()`` reports ``ProcessStatus::NO_CANDIDATE``.

Diagnostics
===========

``num_slots()`` and ``slots_used()`` report capacity and occupancy. The
``contests()``, ``contests_won()``, and ``evictions()`` counters describe table
activity and can be cleared with ``reset_metrics()``. ``dump()`` snapshots the
entries under the table mutex, then invokes the caller's formatting callback
after releasing it.
