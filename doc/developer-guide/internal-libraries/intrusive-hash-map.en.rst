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

.. _lib-intrusive-hash-map:
.. highlight:: cpp
.. default-domain:: cpp

IntrusiveHashMap
****************

:class:`IntrusiveHashMap` provides a "hashed" or "unordered" set, using intrusive links. It provides a
container for elements, each of which has a :arg:`key`. A hash function is applied to a key to
generate a :arg:`hash id` which is used to group the elements in to buckets for fast lookup. This
container is a mix of :code:`std::unordered_set` and :code:`std::unordered_map`. There is no
separation between elements and keys, but each element can contain non-key data.

Iteration over elements is provided and is constant time.

In order to optimize lookup, the container can increase the number of buckets used. This is called
"expansion" and when it occurs is control by the "expansion policy" of the container. The policy can
be to be automatic or only done explicitly.

Usage
*****

To use an :class:`IntrusiveHashMap` the element must provide support for the container. This is done
through an associated descriptor class which provides the operations needed to manipulate the elements
in the container.

Examples
========

Details
*******

.. class:: template < typename H > IntrusiveHashMap

   :tparam H: Element operations.

   An unordered map using a hash function. The properties of the map are determined by types and
   operations provided by the descriptor type :arg:`H`. The following types are derived from :arg:`H`
   and defined in the container type.

   .. type:: value_type

      The type of elements in the container, deduced from the return types of the link accessor methods
      in :arg:`H`.

   .. type:: key_type

      The type of the key used for hash computations. Deduced from the return type of the key
      accessor. An instance of this type is never default constructed nor modified, therefore it can
      be a reference if the key type is expensive to copy.

   .. type:: hash_id

      The type of the hash of a :type:`key_type`. Deduced from the return type of the hash function.
      This must be a numeric type.

   :arg:`H`
      This describes the hash map, primarily via the operations required for the map. The related types are deduced
      from the function return types. This is designed to be compatible with :class:`IntrusiveDList`.

      .. function:: static key_type key_of(value_type * v)

         Key accessor - return the key of the element :arg:`v`.

      .. function:: static hash_id hash_of(key_type key)

         Hash function - compute the hash value of the :arg:`key`.

      .. function:: static bool equal(key_type lhs, key_type rhs)

         Key comparison - two keys are equal if this function returns :code:`true`.

      .. function:: static IntrusiveHashMap::value_type * & next_ptr(IntrusiveHashMap::value_type * v)

         Return a reference to the next element pointer embedded in the element :arg:`v`.

      .. function:: static IntrusiveHashMap::value_type * & prev_ptr(IntrusiveHashMap::value_type * v)

         Return a reference to the previous element pointer embedded in the element :arg:`v`.

   .. type:: iterator

      An STL compliant iterator over elements in the container.

   .. type:: range

      An STL compliant half open range of elements, represented by a pair of iterators.

   .. function:: IntrusiveHashMap & insert(value_type * v)

      Insert the element :arg:`v` into the container. If there are already elements with the same
      key, :arg:`v` is inserted after these elements.

      There is no :code:`emplace` because :arg:`v` is put in the container, not a copy of :arg:`v`.
      For the same reason :arg:`v` must be constructed before calling this method, the container
      will never create an element instance.

   .. function:: iterator begin()

      Return an iterator to the first element in the container.

   .. function:: iterator end()

      Return an iterator to past the last element in the container.

   .. function:: iterator find(value_type * v)

      Search for :arg:`v` in the container. If found, return an iterator referring to :arg:`v`. If not
      return the end iterator. This validates :arg:`v` is in the container.

   .. function:: range equal_range(key_type key)

      Find all elements with a key that is equal to :arg:`key`. The returned value is a half open
      range starting with the first matching element to one past the last matching element. All
      element in the range will have a key that is equal to :arg:`key`. If no element has a matching
      key the range will be empty.

   .. function:: IntrusiveHashMap & erase(iterator spot)

      Remove the element referred to by :arg:`spot` from the container.

   .. function:: iterator iterator_for(value_type * v)

      Return an iterator for :arg:`v`. This is very fast, faster than :func:`IntrusiveHashMap::find`
      but less safe because no validation done on :arg:`v`. If it not in the container (either in no
      container or a different one) further iteration on the returned iterator will go badly. It is
      useful inside range :code:`for` loops when it is guaranteed the element is in the container.

   .. function:: template <typename F> IntrusiveHashMap & apply(F && f)

      :tparam F: A functional type with the signature :code:`void (value_type*)`.

      This applies the function :arg:`f` to every element in the container in such a way that
      modification of the element does not interfere with the iteration. The most common use is to
      :code:`delete` the elements during cleanup. The common idiom ::

         for ( auto & elt : container) delete &elt;

      is problematic because the iteration links are in the deleted element causing the computation
      of the next element to be a use after free. Using :func:`IntrusiveHashMap::apply` enables safe
      cleanup. ::

         container.apply([](value_type & v) { delete & v; });

      Because the links are intrusive it is possible for other classes or the element class to
      modify them. In such cases this method provides a safe way to invoke such mechanisms.

Design Notes
************

This is a refresh of an previously existing class, :code:`TSHahTable`. The switch to C++ 11 and then
C++ 17 made it possible to do much better in terms of the internal implementation and API. The
overall functionality is the roughly the same but with an easier API, compatibility with
:class:`IntrusiveDList`, improved compliance with STL container standards, and better internal
implementation.

The biggest change is that elements are stored in a single global list rather than per hash bucket.
The buckets serve only as entry points in to the global list and to count the number of elements
per bucket. This simplifies the implementation of iteration, so that the old :code:`Location` nested
class can be removed. Elements with equal keys can be handled in the same way as with STL
containers, via iterator ranges, instead of a custom pseudo-iterator class.

Notes on :func:`IntrusiveHashMap::apply`
========================================

This was added after some experience with use of the container. Initially it was added to make
cleaning up the container easier. Without it, cleanup looks like ::

   for ( auto spot = map.begin(), limit = map.end() ; spot != limit ; delete &( * spot++)) {
      ; // empty
   }

Instead one can do ::

   map.apply([](value_type& v) { delete &v; });

The post increment operator guarantees that :arg:`spot` has been updated before the current element is destroyed.
However, it turns out to be handy in other map modifying operations. In the unit tests there is
this code

.. literalinclude:: ../../../src/tscore/unit_tests/test_IntrusiveHashMap.cc
   :lines: 129-132

This removes all elements that do not have the payload "dup". As another design note,
:func:`IntrusiveHashMap::iterator_for` here serves to bypass validation checking on the target for
:func:`IntrusiveHashMap::erase`, which is proper because :func:`IntrusiveHashMap::apply` guarantees
:arg:`thing` is in the map.

Without :code:`apply` this is needed ::

  auto idx = map.begin();
  while (idx != map.end()) {
    auto x{idx++};
    if ("dup"sv != x->_payload) {
      map.erase(x);
    }
  }

The latter is more verbose and more importantly less obvious, depending on a subtle interaction with
post increment.
