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

MemSpan
*******

Synopsis
========

.. code-block:: cpp

    #include <ts/MemSpan.h>

:class:`MemSpan` is a view on a contiguous section of writeable memory. A view does not own the memory
and neither allocates nor de-allocates. The memory in the view is always owned by some other container
and it is the responsibility of the code to make sure the lifetime of the view is no more than that
of the owning container [#vector]_.


Description
===========

A :class:`MemSpan` is generally constructed on either an array or an allocated buffer. This allows
the buffer to be passed around with intrinsic length information. The buffer can also be treated as
an array of varying types, which makes working with serialized data much easier.

Reference
=========

.. class:: MemSpan

   A span of writable memory. Because this is a chunk of memory, conceptually delimited by start and
   end pointers, the sizing type is :code:`ptrdiff_t` so that all of the sizing is consistent with
   differences between pointers. The memory is owned by some other object and that object must
   maintain the memory as long as the span references it.

   .. function:: MemSpan(void * ptr, ptrdiff_t size)

      Construct a view starting at :arg:`ptr` for :arg:`size` bytes.

   .. function:: void * data()  const

      Return a pointer to the first byte of the span.

   .. function:: ptrdiff_t size() const

      Return the size of the span.

   .. function:: bool operator == (MemSpan const& that) const

      Check the equality of two spans, which are equal if they contain the same number of bytes of the same values.

   .. function:: bool is_same(MemSpan const& that) const

      Check if :arg:`that` is the same span as :arg:`this`, that is the spans contain the exact same bytes.

   .. function:: template < typename V > V at(ptrdiff_t n) const

      Return a value of type :arg:`V` as if the span were are array of type :arg:`V`.

   .. function:: template < typename V > V * ptr(ptrdiff_t n) const

      Return a pointer to a value of type :arg:`V` as if the span were are array of type :arg:`V`.

   .. function:: MemSpan prefix(ptrdiff_t n) const

      Return a new instance that contains the first :arg:`n` bytes of the current span. If :arg:`n`
      is larger than the number of bytes in the span, only that many bytes are returned.

   .. function:: MemSpan& remove_prefix(ptrdiff_t n)

      Remove the first :arg:`n` bytes of the span. If :arg:`n` is more than the number of bytes in
      the span the result is an empty span. A reference to the instance is returned.

.. rubric:: Footnotes

.. [#vector]

   Strong caution must be used with containers such as :code:`std::vector` or :code:`std::string`
   because the lifetime of the memory can be much less than the lifetime of the container. In
   particular, adding or removing any element from a :code:`std::vector` can cause a re-allocation,
   invalidating any view of the original memory. In general views should be treated like iterators,
   suitable for passing to nested function calls but not for storing.
