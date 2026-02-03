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

.. include:: ../../../../common.defs

.. _udi-table:

UdiTable
********

The ``ts::UdiTable`` class implements the Udi "King of the Hill" algorithm
(US Patent 7533414) for tracking entities with bounded memory.

Background
==========

The Udi algorithm was developed specifically to address abuse detection. From
the patent:

   "A screening list includes event IDs and associated count values. A pointer
   cyclically selects entries in the table, advancing as events are received.
   An incoming event ID is compared with the event IDs in the table. If the
   incoming event ID matches an event ID in the screening list, the associated
   count is incremented. Otherwise, the count of a selected table entry is
   decremented. If the count value of the selected entry falls to zero, it is
   replaced with the incoming event and the count is reset to one."

The table serves as a **screening list** - the "hot" items to investigate.
Each slot can be examined to determine which top talkers require action.
This two-step approach is important: the table identifies the top N talkers
(where N is the table size), and then each can be investigated to decide
if action is needed.

Synopsis
========

.. code-block:: cpp

   #include "tsutil/UdiTable.h"

   // Define your data type (user data only - key and score are managed by UdiTable)
   struct MyData {
     std::atomic<uint32_t> error_count{0};
     std::atomic<uint32_t> success_count{0};
   };

   // Create table with 10000 slots
   ts::UdiTable<std::string, MyData> table(10000);

   // Process an event (creates or updates entry)
   auto data = table.process_event("some_key", 1);
   if (data) {
     data->error_count.fetch_add(1);
   }

   // Find existing entry
   auto found = table.find("some_key");
   if (found) {
     // Use found->...
   }

Key Properties
==============

**Fixed Memory**
   The table allocates a fixed number of slots at construction time. Memory usage
   is bounded regardless of how many unique keys are tracked.

**Self-Cleaning**
   No background cleanup thread is needed. The table manages eviction automatically
   through the contest mechanism.

**Hot Tracking**
   Frequently-accessed keys naturally stay in the table because they have higher
   scores and win contests against less active entries.

**Global Contest**
   The contest pointer rotates through all slots globally, ensuring fair distribution
   across the entire table regardless of key hash distribution.

**Simple Locking**
   A single mutex protects all operations. All operations are serialized.
   This is simple to reason about and sufficient for most use cases.

**Safe References**
   Returns ``shared_ptr<Data>`` so callers hold safe references even after
   the slot is evicted. No use-after-free concerns.

**Simple API**
   The table owns key and score internally. Users only provide their custom data type.

Algorithm
=========

When a new key is processed and it's not already in the table:

1. The table picks a "contest slot" using a rotating pointer
2. The new key's score is compared against the existing slot's score
3. If the new key has a higher score, it takes the slot (evicting the old entry)
4. If the existing slot has a higher score, it survives but its score is decremented

This ensures that high-activity keys stay in the table while low-activity keys
are eventually evicted.

Template Parameters
===================

.. code-block:: cpp

   template <typename Key,
             typename Data,
             typename Hash = std::hash<Key>>
   class UdiTable;

``Key``
   The key type used to identify entries. Must be hashable and default-constructible.
   Common choices: ``std::string``, ``swoc::IPAddr``.

``Data``
   User's custom data type to associate with each entry. The table owns the key
   and score internally. Must be default-constructible.

``Hash``
   Hash function for keys. Defaults to ``std::hash<Key>``.

Constructor
===========

.. code-block:: cpp

   explicit UdiTable(size_t num_slots);

``num_slots``
   Total number of slots to allocate. Memory usage is approximately
   ``num_slots * (sizeof(Key) + sizeof(uint32_t) + sizeof(shared_ptr<Data>))``.

Methods
=======

process_event
-------------

.. code-block:: cpp

   std::shared_ptr<Data> process_event(Key const& key, uint32_t score_delta = 1);

Process an event for a key. If the key exists, increments its score and returns
the data. If the key doesn't exist, attempts to contest for a slot using the
Udi algorithm.

Returns ``nullptr`` if the key lost the contest and couldn't get a slot.

The returned ``shared_ptr`` remains valid even if the slot is later evicted.

Thread-safe: uses mutex lock.

find
----

.. code-block:: cpp

   std::shared_ptr<Data> find(Key const& key);
   std::shared_ptr<Data const> find(Key const& key) const;

Find an existing entry by key. Returns ``nullptr`` if not found.

The returned ``shared_ptr`` remains valid even if the slot is later evicted.

Thread-safe: uses mutex lock.

remove
------

.. code-block:: cpp

   bool remove(Key const& key);

Remove a key from the table regardless of its score.

Returns ``true`` if the key was found and removed.

Note: Existing ``shared_ptr`` references to the removed data remain valid.

Thread-safe: uses mutex lock.

Statistics
----------

.. code-block:: cpp

   size_t num_slots() const;        // Total allocated slots
   size_t slots_used() const;       // Currently occupied slots
   uint64_t contests() const;       // Total contest attempts
   uint64_t contests_won() const;   // Contests won by new keys
   uint64_t evictions() const;      // Keys evicted due to contest loss

reset_metrics
-------------

.. code-block:: cpp

   void reset_metrics();

Reset table-level metrics (contests, contests_won, evictions) to zero and
update the reset timestamp. Does not modify any tracked entries.

dump
----

.. code-block:: cpp

   std::string dump(data_format_fn format_data = nullptr) const;

Dump all entries to a string for debugging. The format function signature is:

.. code-block:: cpp

   std::string format_data(Key const& key, uint32_t score, std::shared_ptr<Data> const& data);

Example: IP Address Tracking
============================

.. code-block:: cpp

   #include "tsutil/UdiTable.h"
   #include "swoc/swoc_ip.h"

   // User data only - IP address and score are managed by UdiTable
   struct IPData {
     std::atomic<uint32_t> error_count{0};
     std::atomic<uint64_t> blocked_until{0};
   };

   // Create table with 50000 slots
   ts::UdiTable<swoc::IPAddr, IPData> table(50000);

   // Track an error from an IP
   void record_error(const swoc::IPAddr& ip) {
     if (auto data = table.process_event(ip, 1)) {
       data->error_count.fetch_add(1);
       if (data->error_count.load() > 100) {
         // Block this IP for 5 minutes
         data->blocked_until.store(now_ms() + 300000);
       }
     }
   }

   // Check if IP is blocked
   bool is_blocked(const swoc::IPAddr& ip) {
     if (auto data = table.find(ip)) {
       uint64_t until = data->blocked_until.load();
       return until > 0 && now_ms() < until;
     }
     return false;
   }

Thread Safety
=============

The ``UdiTable`` is thread-safe for concurrent access using a single ``std::mutex``:

- All operations (``find()``, ``process_event()``, ``remove()``) use the same mutex
- All operations are serialized

This simple locking model is easy to reason about and sufficient for most use cases.
For high-contention scenarios, partitioned locking could be added as a future enhancement.

**Safe Shared Ownership**

The ``find()`` and ``process_event()`` methods return ``std::shared_ptr<Data>``.
This provides safe shared ownership:

- The returned pointer remains valid even after the slot is evicted
- The ``Data`` object lives until all ``shared_ptr`` references are released
- No use-after-free concerns

Memory Sizing
=============

The memory usage is approximately:

.. code-block:: text

   Total = num_slots * sizeof(Slot) + sizeof(lookup_map) + sizeof(mutex)

   where Slot = Key + uint32_t (score) + shared_ptr<Data>

For IP tracking with an ``IPData`` of approximately 16 bytes and ``swoc::IPAddr``
of approximately 24 bytes:

- 10,000 slots ≈ 0.6 MB
- 50,000 slots ≈ 3.0 MB
- 100,000 slots ≈ 6.0 MB

Note: Each entry has a heap-allocated ``Data`` object managed by ``shared_ptr``.

See Also
========

- :ref:`admin-plugins-abuse_shield` - Uses UdiTable for IP abuse tracking
