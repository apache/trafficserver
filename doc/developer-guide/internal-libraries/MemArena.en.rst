.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements. See the NOTICE file distributed with this work for
   additional information regarding copyright ownership. The ASF licenses this file to you under the
   Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with
   the License. You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

.. include:: ../../common.defs
.. highlight:: cpp
.. default-domain:: cpp
.. |MemArena| replace:: :class:`MemArena`

.. _MemArena:

MemArena
*************

|MemArena| provides a memory arena or pool for allocating memory. Internally |MemArena| reserves
memory in large blocks and allocates pieces of those blocks when memory is requested. Upon
destruction all of the reserved memory is released which also destroys all of the allocated memory.
This is useful when the goal is any (or all) of trying to

*  amortize allocation costs for many small allocations.
*  create better memory locality for containers.
*  de-allocate memory in bulk.

Description
+++++++++++

When a |MemArena| instance is constructed no memory is reserved. A hint can be provided so that the
first internal reservation of memory will have close to but at least that amount of free space
available to be allocated.

In normal use memory is allocated from |MemArena| using :func:`MemArena::alloc` to get chunks
of memory, or :func:`MemArena::make` to get constructed class instances. :func:`MemArena::make`
takes an arbitrary set of arguments which it attempts to pass to a constructor for the type
:code:`T` after allocating memory (:code:`sizeof(T)` bytes) for the object. If there isn't enough
free reserved memory, a new internal block is reserved. The size of the new reserved memory will be at least
the size of the currently reserved memory, making each reservation larger than the last.

The arena can be **frozen** using :func:`MemArena::freeze` which locks down the currently reserved
memory and forces the internal reservation of memory for the next allocation. By default this
internal reservation will be the size of the frozen allocated memory. If this isn't the best value a
hint can be provided to the :func:`MemArena::freeze` method to specify a different value, in the
same manner as the hint to the constructor. When the arena is thawed (unfrozen) using
:func:`MemArena::thaw` the frozen memory is released, which also destroys the frozen allocated
memory. Doing this can be useful after a series of allocations, which can result in the allocated
memory being in different internal blocks, along with possibly no longer in use memory. The result
is to coalesce (or garbage collect) all of the in use memory in the arena into a single bulk
internal reserved block. This improves memory efficiency and memory locality. This coalescence is
done by

#. Freezing the arena.
#. Copying all objects back in to the arena.
#. Thawing the arena.

Because the default reservation hint is large enough for all of the previously allocated memory, all
of the copied objects will be put in the same new internal block. If this for some reason this
sizing isn't correct a hint can be passed to :func:`MemArena::freeze` to specify a different value
(if, for instance, there is a lot of unused memory of known size). Generally this is most useful for
data that is initialized on process start and not changed after process startup. After the process
start initialization, the data can be coalesced for better performance after all modifications have
been done. Alternatively, a container that allocates and de-allocates same sized objects (such as a
:code:`std::map`) can use a free list to re-use objects before going to the |MemArena| for more
memory and thereby avoiding collecting unused memory in the arena.

Other than a freeze / thaw cycle, there is no mechanism to release memory except for the destruction
of the |MemArena|. In such use cases either wasted memory must be small enough or temporary enough
to not be an issue, or there must be a provision for some sort of garbage collection.

Generally |MemArena| is not as useful for classes that allocate their own internal memory
(such as :code:`std::string` or :code:`std::vector`), which includes most container classes. One
container class that can be easily used is :class:`IntrusiveDList` because the links are in the
instance and therefore also in the arena.

Objects created in the arena must not have :code:`delete` called on them as this will corrupt
memory, usually leading to an immediate crash. The memory for the instance will be released when the
arena is destroyed. The destructor can be called if needed but in general if a destructor is needed
it is probably not a class that should be constructed in the arena. Looking at
:class:`IntrusiveDList` again for an example, if this is used to link objects in the arena, there is
no need for a destructor to clean up the links - all of the objects will be de-allocated when the
arena is destroyed. Whether this kind of situation can be arranged with reasonable effort is a good
heuristic on whether |MemArena| is an appropriate choice.

While |MemArena| will normally allocate memory in successive chunks from an internal block, if the
allocation request is large (more than a memory page) and there is not enough space in the current
internal block, a block just for that allocation will be created. This is useful if the purpose of
|MemArena| is to track blocks of memory more than reduce the number of system level allocations.

Reference
+++++++++

.. class:: MemArena

   .. function:: MemArena(size_t n)

      Construct a memory arena. :arg:`n` is optional. Initially not memory is reserved. If :arg:`n`
      is provided this is a hint that the first internal memory reservation should provide roughly
      and at least :arg:`n` bytes of free space. Otherwise the internal default hint is used. A call
      to :code:`alloc(0)` will not allocate memory but will force the reservation of internal memory
      if this should be done immediately rather than lazily.

   .. function:: MemSpan alloc(size_t n)

      Allocate memory of size :arg:`n` bytes in the arena. If :arg:`n` is zero then internal memory
      will be reserved if there is currently none, otherwise it is a no-op.

   .. function:: template < typename T, typename ... Args > T * make(Args&& ... args)

      Create an instance of :arg:`T`. :code:`sizeof(T)` bytes of memory are allocated from the arena
      and the constructor invoked. This method takes any set of arguments, which are passed to
      the constructor. A pointer to the newly constructed instance of :arg:`T` is returned. Note if
      the instance allocates other memory that memory will not be in the arena. Example constructing
      a :code:`std::string_view` ::

         std::string_view * sv = arena.make<std::string_view>(pointer, n);

   .. function:: MemArena& freeze(size_t n)

      Stop allocating from existing internal memory blocks. These blocks are now "frozen". Further
      allocation calls will cause new memory to be reserved.

      :arg:`n` is optional. If not provided, make the hint for the next internal memory reservation
      to be large enough to hold all currently (now frozen) memory allocation. If :arg:`n` is
      provided it is used as the reservation hint.

   .. function:: MemArena& thaw()

      Release all frozen internal memory blocks, destroying all frozen allocations.

   .. function:: MemArena& clear(size_t n)

      Release all memory, destroying all allocations. The next memory reservation will be the size
      of the allocated memory (frozen and not) at the time of the call to :func:`MemArena::clear`.
      :arg:`n` is optional. If this is provided it is used as the hint for the next reserved block,
      otherwise the hint is the size of all allocated memory.

Internals
+++++++++

Allocated memory is tracked by two linked lists, one for current memory and the other for frozen
memory. The latter is used only while the arena is frozen. Because a shared pointer is used for the
link, the list can be de-allocated by clearing the head pointer in |MemArena|. This pattern is
similar to that used by the :code:`IOBuffer` data blocks, and so those were considered for use as
the internal memory allocation blocks. However, that would have required some non-trivial tweaks and,
with the move away from internal allocation pools to memory support from libraries like "jemalloc",
unlikely to provide any benefit.
