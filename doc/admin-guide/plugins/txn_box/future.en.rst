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

   Copyright 2022, Alan M. Carroll

.. include:: txnbox_common.defs

.. highlight:: yaml

.. _future:

************
Future Work
************

This is future intended work and so may change radically. The essence should remain similar.

Session
=======

inbound-remote-port

inbound-local-port

Features
********

ts-uuid
   Process UUID for Traffic Server.

Feature Modifiers
=================

The extracted feature can be post processed using options in the :txb:drtv:`with`. This is done by
having a pair where the first element is the feature extraction, and the second is a map of options.
Currently the only planned modifier is "hash".

slice
   Extract elements of a list. This takes two arguments, the left and right slice points. These are
   positions between elements of a list. Position 0 is before any element, and position -0
   (represented by "*") is past the last element. Other slice points count up from 0 (1, 2, 3, ...)
   left to right and down from -0 ( -1, -2, -3, ...) right to left.

Comparisons
***********

Directives
**********

call
   "call: <plugin>"

   "call: [ <plugin>, <args>, ... ]"

   Invoke a callback provided by a plugin.

   .. note:: Implementation

      Should the entry point be specifiable in the directive? That could be very nice.


Feature Tuples
**************

Do_with a list feature, the matching is done across elements of the list. This can be done in an iterative
style where a comparison is made against each element in the list, or tuple style where there is a
different comparison for each element in the list.

The list style matching operators require a value that is another comparison.

:code:`for-all`
    The match succeeds if the base comparison succeeds for every element.

:code:`for-any`
    The match succeeds if the base comparison succeeds for any element.

:code:`for-none`
    The match succeeds if the base comparison fails for all elements.

The tuple style match is :code:`tuple`. It requires a list of comparisons and applies the
comparisons against the list in the same order. It matches if all of the comparisons match. Elements
that do not have a comparison do not match. This means by default if the feature list is a different
length that the comparison list, the match will fail. This is the common case. For less common cases
there are other options.

Tuple elements can be skipped with the :code:`whatever` comparison which accepts any feature type
and always matches.

Trailing elements can be matched with any of the list comparisons. This must always be the last
comparison in a tuple and applies to all elements that do not have an explicit comparison.

These can be combined to ignore all elements past a fixed initial set by using a list comparison
after the last significant comparison. ::

   -  for-all:
      -  otherwise:

This is useful if there are different comparisons in the same selection. Otherwise it might be
better to use modifiers to shape the list. E.g., if only the first two elements are relevant then
the :code:`slice` modifier can be used to reduce the list to the first two elements. Using
modifiers is faster and more compact but has the cost of limiting all of the comparisons in the
selection.

Matching can be done in a more explicitly iterative style by use of :code:`...` and modifiers. This
can be used to process successively smaller subsequences of the list.

For examples of all this, consider working with the `Via` header. This is a multi-valued field.
Suppose it was required to check for having been through the local instance of Traffic Server by
looking for the process UUID in the fields. If the first element is the current instance, that's
a direct loop and an error. Otherwise, if the UUID is any other element that is an error unless
the `k8-routing` field is present, indicating that there is active routing that sent it back.

.. note:: This is a real life example.

The design here is to split the `Via` header and then work with the list. The :code:`ts-uuid`
extractor gets the UUID for the Traffic Server process which is used in the `Via` header. ::

   with: [ creq-field@Via , { split: "," } ] # split in to list
   select:
   -  tuple:
      -  contains: ts-uuid # only check first
      -  for-all:
         - whatever:
      do:
      -  txn-status: [ 400 , "Loop detected" ]
   -  tuple:
      -  whatever: # skip first element.
      -  for-any: # otherwise, see if it's any other element.
         -  contains: ts-uuid
      do: # found it, fail if there's no routing flag.
      -  with: creq-field@k8-routing::present
         select:
         -   eq: false
             do:
                txn-status: [ 400 , "Loop detected" ]

Issues

*  Matching on just the first value is annoyingly verbose. This would be noticeably better if there
   was an "apply" directive which loaded the :code:`with` context, e.g. regular expression groups
   and :code:`...` without even trying to do matches.

*  Do_with support for :code:`do` in each comparison, this may be of more limited utility. But that
   would be verbose to (for instance) do something for every tuple with a specific first element
   if there are multiple cases that match with that element.
