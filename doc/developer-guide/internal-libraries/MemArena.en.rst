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
.. |MemArena| replace:: :class:`MemArena`

.. _MemArena:

MemArena
*************

|MemArena| provides a memory arena or pool for allocating memory. The intended use is for allocating many small chunks of memory - few, large allocations are best handled independently. The purpose is to amortize the cost of allocation of each chunk across larger allocations in a heap style. In addition the allocated memory is presumed to have similar lifetimes so that all of the memory in the arena can be de-allocatred en masse. This is a memory allocation style used by many cotainers - elements are small and allocated frequently, but all elements are discarded when the container itself is destroyed.

Description
+++++++++++

|MemArena| manages an internal list of memory blocks, out of which it provides allocated
blocks of memory. When an instance is destructed all the internal blocks are also freed. The
expected use of this class is as an embedded memory manager for a container class.

To support coalescence and compaction of memory, the methods :func:`MemArena::freeze` and
:func:`MemArena::thaw` are provided. These create in effect generations of memory allocation.
Calling :func:`MemArena::freeze` marks a generation. After this call any further allocations will
be in new internal memory blocks. The corresponding call to :func:`MemArena::thaw` cause older
generations of internal memory to be freed. The general logic for the container would be to freeze,
re-allocate and copy the container elements, then thaw. This would result in compacted memory
allocation in a single internal block. The uses cases would be either a process static data
structure after initialization (coalescing for locality performence) or a container that naturally
re-allocates (such as a hash table during a bucket expansion). A container could also provide its
own API for its clients to cause a coalesence.

Other than freeze / thaw, this class does not offer any mechanism to release memory beyond its destruction. This is not an issue for either process globals or transient arenas.

Internals
+++++++++

|MemArena| opperates in *generations* of internal blocks of memory. Each generation marks a series internal block of memory. Allocations always occur from the most recent block within a generation, as it is always the largest and has the most unallocated space. The most recent block (current) is also the head of the linked list of memory blocks. Allocations are given in the form of a :class:`MemSpan`. Once an internal block of memory has exhausted it's avaliable space, a new, larger, internal block will be added to the generation. Say this is the current arena state:

.. uml::
   :align: center

   component [block] as b1
   component [block] as b2
   component [block] as b3
   component [block] as b4
   component [block] as b5
   component [block] as b6

   b1 -> b2
   b2 -> b3
   b3 -> b4
   b4 -> b5
   b5 -> b6

   generation -u- b3
   current -u- b1

A call to :func:`MemArena::thaw` will deallocate any generation that is not the current generation. Thus, currently it is impossible to deallocate ie. just the third generation. Everything after the generation pointer is in previous generations and everything before, and including, the generation pointer is in the current generation. Since blocks are reference counted, thawing is just a single assignment to drop everything after the generation pointer. After a :func:`MemArena::thaw`:

.. uml::
   :align: center

   component [block] as b3
   component [block] as b4
   component [block] as b5
   component [block] as b6


   b3 -> b4
   b4 -> b5
   b5 -> b6

   current -u- b3
   generation -u- b6

A generation can only be updated with an explicit call to :func:`MemArena::freeze`. The next generation is not actually allocated until a call to :func:`MemArena::alloc` happens. On the :func:`MemArena::alloc` following a :func:`MemArena::freeze` the next internal block of memory is the larger of the sum of all current allocations or the number of bytes requested. The reason for this is that the caller could :func:`MemArena::alloc` a size larger than all current allocations at which point if we were to resize earlier, an internal block would be wasted. After a :func:`MemArena::freeze`:

.. uml::
   :align: center

   component [block] as b3
   component [block] as b4
   component [block] as b5
   component [block] as b6


   b3 -> b4
   b4 -> b5
   b5 -> b6

   current -u- b3

After the next :func:`MemArena::alloc`:

.. uml::
   :align: center

   component [block\nnew generation] as b3
   component [block] as b4
   component [block] as b5
   component [block] as b6
   component [block] as b7


   b3 -> b4
   b4 -> b5
   b5 -> b6
   b6 -> b7

   generation -u- b3
   current -u- b3

A caller can actually :func:`MemArena::alloc` **any** number of bytes. Internally, if the arena is unable to allocate enough memory for the allocation, it will create a new internal block of memory large enough and allocate from that. So if the arena is allocated like:

.. code-block:: cpp

   ts::MemArena *arena = new ts::MemArena(64);

The caller can actually allocate more than 64 bytes.

.. code-block:: cpp

   ts::MemSpan span1 = arena->alloc(16);
   ts::MemSpan span1 = arena->alloc(256);

Now, span1 and span2 are in the same generation and can both be safely used. After:

.. code-block:: cpp

   arena->freeze();
   ts::MemSpan span3 = arena->alloc(512);
   arena->thaw();

span3 can still be used but span1 and span2 have been deallocated and usage is undefined.

Internal blocks are adjusted for optimization. Each :class:`MemArena::Block` is just a header for the underlying memory it manages. The header and memory are allocated together for locality such that each :class:`MemArena::Block` is immediately followed with the memory it manages. If a :class:`MemArena::Block` is larger than a page (defaulted at 4KB), it is aligned to a power of two. The actual memory that a :class:`MemArena::Block` can allocate out is slightly smaller. This is because a portion of the allocated memory is reserved for the header. Another 16 bytes is reserved to track the allocation headers used by malloc; for page alignment. ie, the default block size is 32768 bytes, but it will only be able to allocate out 32720 bytes.

Reference
+++++++++

.. class:: MemArena

   .. class:: Block

      Underlying memory allocated is owned by the :class:`Block`. A linked list.

      .. member:: size_t size
      .. member:: size_t allocated
      .. member:: std::shared_ptr<Block> next
      .. function:: Block(size_t n)
      .. function:: char* data()

   .. function:: MemArena()

      Construct an empty arena.

   .. function:: explicit MemArena(size_t n)

      Construct an arena with :arg:`n` bytes.

   .. function:: MemSpan alloc(size_t n)

      Allocate an :arg:`n` byte chunk of memory in the arena.

   .. function:: MemArena& freeze(size_t n = 0)

      Block all further allocation from any existing internal blocks. If :arg:`n` is zero then on the next allocation request a block twice as large as the current generation, otherwise the next internal block will be large enough to hold :arg:`n` bytes.

   .. function:: MemArena& thaw()

      Unallocate all internal blocks that were allocated before the current generation.

   .. function:: MemArena& empty()

      Empties the entire arena and deallocates all underlying memory. Next block size will be equal to the sum of all allocations before the call to empty.

   .. function:: size_t size() const

      Get the current generation size. The default size of the arena is 32KB unless otherwise specified.

   .. function:: size_t remaining() const

      Amount of space left in the generation.

   .. function:: size_t allocated_size() const

      Total number of bytes allocated in the arena.

   .. function:: size_t unallocated_size() const

      Total number of bytes unallocated in the arena. Can be used to see the internal fragmentation.

   .. function:: bool contains (void *ptr) const

      Returns whether or not a pointer is in the arena.

   .. function:: Block* newInternalBlock(size_t n, bool custom)

      Create a new internal block and returns a pointer to the block.

   .. member:: size_t arena_size

      Current generation size.

   .. member:: size_t total_alloc

      Number of bytes allocated out.

   .. member:: size_t next_block_size

      Size of next generation.

   .. member:: std::shared_ptr<Block> generation

      Pointer to the current generation.

   .. member:: std::shared_ptr<Block> current

      Pointer to most recent internal block of memory.
