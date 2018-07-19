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

.. _lib-intrusive-list:
.. highlight:: cpp
.. default-domain:: cpp

IntrusiveDList
**************

:class:`IntrusiveDList` is a class that provides a double linked list using pointers embeded in the
object. :class:`IntrusiveDList` also acts as a queue. No memory management is done - objects can be
added to and removed from the list but the allocation and deallocation of the objects must be
handled outside the class. This class supports an STL compliant bidirectional iteration.

Definition
**********

.. class:: template < typename L > IntrusiveDList

   A double linked list / queue based on links inside the objects. The element type, :code:`T`, is
   deduced from the return type of the link accessor methods in :arg:`L`.

   :tparam L: List item descriptor

   The descriptor, :arg:`L`, is a type that provides the operations on list elements required by
   the container.

   .. type:: value_type

      The type of elements in the container, deduced from the return types of the link accessor methods
      in :arg:`L`.

   :arg:`L`
      .. function:: static value_type * & next_ptr(value_type * elt)

         Return a reference to the next element pointer embedded in the element :arg:`elt`.

      .. function:: static value_type * & prev_ptr(value_type * elt)

         Return a reference to the previous element pointer embedded in the element :arg:`elt`.

   .. function:: value_type * head()

      Return a pointer to the head element in the list. This may be :code:`nullptr` if the list is empty.

   .. function:: value_type * tail()

      Return a pointer to the tail element in the list. This may be :code:`nullptr` if the list is empty.

   .. function:: IntrusiveDList & clear()

      Remove all elements from the list. This only removes, no deallocation nor destruction is performed.

   .. function:: size_t count() const

      Return the number of elements in the list.

   .. function:: IntrusiveDList & append(value_type * elt)

      Append :arg:`elt` to the list.

   .. function:: IntrusiveDList & prepend(value_type * elt)

      Prepend :arg:`elt` to the list.

   .. function:: value_type * take_head()

      Remove the head element and return a pointer to it. May be :code:`nullptr` if the list is empty.

   .. function:: value_type * take_tail()

      Remove the tail element and return a pointer to it. May be :code:`nullptr` if the list is empty.

Usage
*****

An instance of :class:`IntrusiveDList` acts as a container for items, maintaining a doubly linked
list / queue of the objects and tracking the number of objects in the container. There are methods
for appending, prepending, and inserting (both before and after a specific element already in the
list). Some care must be taken because it is too expensive to check for an element already being in
the list or in another list. The internal links are set to :code:`nullptr`, therefore one simple check
for being in a list is if either internal link is not :code:`nullptr`. This requires initializing the
internal links to :code:`nullptr`.

Examples
========

In this example the goal is to have a list of :code:`Message` objects. First the class is declared
along with the internal linkage support.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 38-63

The struct :code:`Linkage` is used both to provide the descriptor to :class:`IntrusiveDList` and to
contain the link pointers. This isn't necessary - the links could have been direct members
and the implementation of the link accessor methods adjusted. Because the links are intended to be
used only by a specific container class (:code:`Container`) the struct is made protected.

The implementation of the link accessor methods.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 65-74

A method to check if the message is in a list.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 76-80

The container class for the messages could be implemented as

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 82-99

The :code:`debug` method takes a format string (:arg:`fmt`) and an arbitrary set of arguments, formats
the arguments in to the string, and adds the new message to the list.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 122-131

The :code:`print` method demonstrates the use of the range :code:`for` loop on a list.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 142-148

The maximum severity level can also be computed even more easily using :code:`std::max_element`.
This find the element with the maximum severity and returns that severity, or :code:`LVL_DEBUG` if
no element is found (which happens if the list is empty).

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 134-140

Other methods for the various severity levels would be implemented in a similar fashion. Because the
intrusive list does not do memory management, the container must clean that up itself, as in the
:code:`clear` method. A bit of care must be exercised because the links are in the elements, and
these links are used for iteration therefore using an iterator that references a deleted object is
risky. One approach, illustrated here, is to use :func:`IntrusiveDList::take_head` to remove the
element before destroying it. Another option is to allocation the elements in a :class:`MemArena` to
avoid the need for any explicit cleanup.

.. literalinclude:: ../../../lib/ts/unit-tests/test_IntrusiveDList.cc
   :lines: 106-114

Design Notes
************

The historic goal of this class is to replace the :code:`DLL` list support. The benefits of this are

*  Remove dependency on the C preprocessor.

*  Provide greater flexibility in the internal link members. Because of the use of the descriptor
   and its static methods, the links can be anywhere in the object, including in nested structures
   or super classes. The links are declared like normal members and do not require specific macros.

*  Provide STL compliant iteration. This makes the class easier to use in general and particularly
   in the case of range :code:`for` loops.

*  Track the number of items in the list.

*  Provide queue support, which is of such low marginal expense there is, IMHO, no point in
   providing a separate class for it.



