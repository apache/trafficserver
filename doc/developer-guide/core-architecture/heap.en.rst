.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs
.. highlight:: cpp
.. default-domain:: cpp

.. _core-hdr-heap:

Header Heap
***********

Memory for HTTP header data is kept in :term:`header heap`\s.

Classes
=======

.. class:: HdrHeapObjImpl

   This is the abstract base class for objects allocated in a :class:`HdrHeap`. This allows updating
   objects in a heap in a generic way, without having to locate all of the pointers to the objects.

   The type of an instance stored in a heap must be one of the following values.

   .. enumerator:: HDR_HEAP_OBJ_EMPTY = 0

      Used to mark invalid objects, ones not yet constructed or ones that have been destroyed.

   .. enumerator:: HDR_HEAP_OBJ_RAW = 1

      Some sort of raw object, I have no idea.

   .. enumerator:: HDR_HEAP_OBJ_URL = 2

      A URL object.

   .. enumerator:: HDR_HEAP_OBJ_HTTP_HEADER = 3

      The header for an HTTP request or response.

   .. enumerator:: HDR_HEAP_OBJ_MIME_HEADER = 4

      A MIME header, containing MIME style fields with names and values.

   .. enumerator:: HDR_HEAP_OBJ_FIELD_BLOCK = 5

      Who the heck knows?

.. class:: HdrStrHeap

   This is a :term:`variable sized class`, therefore new instance must be created by :func:`new_HdrStrHeap`
   and deallocated by the :code:`destroy` method.

.. function:: HdrStrHeap * new_HdrStrHeap(int n)

   Create and return a new instance of :class:`HdrStrHeap`. If :arg:`n` is less than ``HDR_STR_HEAP_DEFAULT_SIZE``
   it is increased to that value.

   If the allocated size is ``HDR_STR_HEAP_DEFAULT_SIZE`` (or smaller and upsized to that value) then
   the instance is allocated from a thread local pool via :code:`strHeapAllocator`. If larger it
   is allocated from global memory via :code:`ats_malloc`.

.. class:: HdrHeap

   This is a :term:`variable sized class` and therefore new instances must be created by :func:`new_HdrHeap`
   and deallocated by the :code:`destroy` method.

   :class:`HdrHeap` manages memory for heap objects directly and memory for strings via ancillary
   heaps (which are instances of :class:`HdrStrHeap`). For the string heaps there is at most one
   writeable heap, and up to :code:`HDR_BUF_RONLY_HEAPS` read only heaps.

   All objects in the internal heap must be subclasses of :class:`HdrHeapObjImpl`.

   .. function:: size_t required_space_for_evacuation()

      Calculate and return the total live string space for :arg:`this`.

   .. function:: void evacuate_from_str_heaps(HdrStrHeap * new_heap)

      Copy all live strings from the heap objects in :arg:`this` to :arg:`new_heap`.

   .. function:: void coalesce_str_heaps(int incoming_size)

      This garbage collects the string heaps in a half space style, by creating a new string space
      (string heap), copying all of the strings there, and then discarding the existing string heaps.

      The total amount of live string space is calculated by
      :func:`HdrHeap::required_space_for_evacuation` and a new string heap is created of a size at
      least as large as the live string space plus :arg:`incoming_size` bytes.

      All of the live strings are moved to the new string heap by
      :func:`HdrHeap::evacuate_from_str_heaps`, the existing string heaps are deallocated, and the
      new string heap becomes the writeable string heap for the header heap. The end result is a
      single writeable string heap and no read only string heaps, with all live strings resident in
      that writeable string heap.

   .. function:: char * allocate_str(int bytes)

      Allocate :arg:`nbytes` of space for a string in the writeable string heap. A pointer to the
      first byte is returned, or ``nullptr`` if the space could not be allocated.

   .. function:: HdrHeapObjImpl * allocate_obj(int nbytes, int type)

      Allocate a :arg:`type` object that is :arg:`nbytes` in size in the heap and return a pointer
      to it, or ``nullptr`` if the object could not be allocated.

      :arg:`nbytes` must be at most ``HDR_MAX_ALLOC_SIZE``.

      The members of :class:`HdrHeapObjImpl` are initialized. Further initialization is the
      responsibility of the caller.

      :arg:`type` must be one of the values specified in :class:`HdrHeapObjImpl`.

.. function:: HdrHeap * new_HdrHeap(int n)

   Create and return a new instance of :class:`HdrHeap`. If :arg:`n` is less than ``HDR_HEAP_DEFAULT_SIZE``
   it is increased to that value.

   If the allocated size is ``HDR_HEAP_DEFAULT_SIZE`` (or smaller and upsized to that value) then
   the instance is allocated from a thread local pool via :code:`hdrHeapAllocator`. If larger it
   is allocated from global memory via :code:`ats_malloc`.

.. topic:: Header Heap Class Structure

   .. figure:: /uml/images/hdr-heap-class.svg


Implementation
==============

String Coalescence
------------------

String heaps do do not maintain lists of internal free space. Strings that are released are left in
place, creating dead space in the heap. For this reason it can become necessary to do a garbage
collection operation on the writeable string heap in the header heap by calling
:func:`HdrHeap::coalesce_str_heaps`. This is done when

*  The amount of dead space in the writable string heap exceeds ``MAX_LOST_STR_SPACE``.

*  The header heap is preparing to be serialized.

*  An external string heap is being added and all current read only string heap slots are used.

Each heap object is responsible for providing a :code:`move_strings` method which copies its strings
to a new string heap. This is a source of pointer invalidation for other parts of the core and the
plugin API. For the latter, insulating from such string movement is the point of the
:c:type:`TSMLoc` type.

String Allocation
-----------------

Storage for a string is allocated by :func:`HdrHeap::allocate_str`. If the current amount of dead
space is too large, this is treated as an initial allocation failure. If there is no current
writeable string heap, one is created that is a least as large as the space requested and the size
of the previous writeable string heap. Space for the string is then allocated out of the writeable
string heap. If this fails due to lack of space the current writeable string heap is "demoted" to a
read only string heap and allocation retried (which will cause a new writeable string heap). If the
writeable string heap cannot be demoted due to lack of read only slots, the strings heaps are
coalesced with an additional size request of the requested string size. This will result in a single
writeable string heap and not read only heaps, the former containing all of the existing strings plus
sufficient space to allocate the new string.

.. topic:: Decision Diagram

   .. figure:: /uml/images/hdr-heap-str-alloc.svg

Object Allocation
-----------------

Objects are allocated on the header heap by :func:`HdrHeap::allocate_obj`. Such objects must be one
of a compile time determined set of types [#]_. This method first tries to allocate the object in
existing free space. If that doesn't work then the allocator walks a list of :class:`HdrHeap`
instances looking for space. If no space is found anywhere, a new :class:`HdrHeap` instance is
created with twice the space of the last :class:`HdrHeap` in the list and added to the list to
try.

Once space is found for the object, the base members of :class:`HdrHeapObjImpl` are initialized with
the objec type and size, with the :arg:`m_obj_flags` set to 0.

.. rubric:: Footnotes.

.. [#] Not that I can see any good reason for that, if virtual methods instead of :code:`switch` statements were used.
