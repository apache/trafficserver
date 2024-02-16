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

.. include:: ../../../../common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _comparison_reference:

********************
Comparison Reference
********************

Comparisons
************

Comparison match again the active feature. For most comparisons the value to compare is the value of
the comparison key. In a few cases the comparison value is implicit in the comparison (e.g.
:txb:cmp:`is-true`). These are indicated by the leading "is-".

Each comparison can compares its value against features of specific types. A feature that is not one
of those types is never matched. In some cases the value can be a list, in which each value is
compared against the active feature and the comparison matches if any element of the list matches.
These are marked as such, for example :txb:cmp:`match`.

Special Comparisons
===================

.. comparison:: is-null
   :type: NIL

   Succeed if the active feature is ``NULL``. The most common use for this is with HTTP fields to
   detect if the field is not present. ::

      with: proxy-req-field<Best-Band>:
      select:
      -  is-null: # Field is not present.
         do:
         -  # stuff
      -  match: "" # Field is present but empty.
         do:
         -  # stuff
      -  match: "Delain" # Field is correctly set, including capitalization.
      -  do: # Present but invalid value.
         -  # stuff

   See :cmp:`is-empty` to check for ``NULL`` or the empty string.

String Comparisons
==================

String comparisons can set the active index group extractors. The string comparisons are marked with
which groups, if any, are set by the comparison if it matches. All string comparisons are by default
case sensitive.

The argument ``nc`` (no case) can be passed to make the comparison case insensitive. For  instance
to make sure the field "Best-Band" is capitalized correctly ::

   with: ua-req-field<Best-Band>
   select:
   -  match: "Delain" # Exactly "Delain"
      # Do nothing, it's correct.
   -  match<nc>: "Delain" # wrong capitalization like "delain" or "delaiN"
      do:
      -  ua-req-field<Best-Band>: "Delain" # fix it.

======= ==========================================================
Index   Content
======= ==========================================================
0       The matched string.
1..N    Regular expression capture groups.
*       The unmatched string.
======= ==========================================================

See :cmp:`none-of` for handling negative string matches. This makes it easy to match features that
do **not** contain a string.


.. txb:comparison:: match
   :type: string
   :tuple:
   :groups: 0,*

   Exact string match.

.. txb:comparison:: prefix
   :type: string
   :tuple:
   :groups: 0,*

   Prefix string match. Successful if the value is a prefix of the feature.

.. txb:comparison:: suffix
   :type: string
   :tuple:
   :groups: 0,*

   Suffix string match. Successful if the value is a suffix of the feature.

.. txb:comparison:: tld
   :type: string
   :tuple:
   :groups: 0,*

   Top level domain matching. This is similar to :cmp:`suffix` but has a special case for the "."
   separator for domains. It will match the exact feature, or as a suffix if there is an intervening
   ".". ::

      tld: "yahoo.com"

   is equivalent to ::

      any-of:
      - suffix: ".yahoo.com"
      - match: "yahoo.com"

.. txb:comparison:: contains
   :type: string
   :groups: 0

   Substring checking. This matches if the active feature has the value as a substring.

   If the feature is "potzrebie" then ::

      -  contains: "pot" # success
      -  contains: "zri" # failure
      -  contains: "zreb" # success

.. comparison:: path
   :type: string
   :groups: 0,*

   This is a literal match which accepts an optional trailing "/" character. This is useful for
   matching a specific path in a URL because the request may or may not add the trailing character.
   That is ::

      path: "albums"

   will match a path of "albums" or "albums/". I.e. it is identical to ::

      any-of:
      -  match: "albums"
      -  match: "albums/"

   and to ::

      path: "albums/"

.. txb:comparison:: rxp
   :type: string
   :groups: 0,1..N

   Regular expression comparison. If this matches the index scoped extractors are set. Index 0 is
   the entire match, index 1 is the first capture group, etc.

.. comparison:: is-empty
   :type: NIL, string

   Successful if the active feature is ``NULL`` or the empty string. This is identical to ::

      any-of:
      -  is-null:
      -  match: ""

   This is sufficiently common to justify having a specific comparison. If the actions for having no
   value, either because it's missing or has no value are the same, then the example from
   :cmp:`is-null` can be done more cleanly as ::

      with: proxy-req-field<Best-Band>:
      select:
      -  is-empty: # Field has no value - missing or empty.
         do:
         -  # stuff
      -  match: "Delain" # Field is correctly set, including capitalization.
      -  do: # Present but invalid value.
         -  # stuff



Numeric Comparisons
===================

.. txb:comparison:: eq
   :type: integer, boolean, IP address

   Equal. Successful if the value is equal to the feature. This works for numeric and IP address features.

.. txb:comparison:: ne
   :type: integer, boolean, IP address

   Not equal. Successful if the value is not equal to the feature. This is valid for Integers and
   IP Addresses.

.. txb:comparison:: lt

   Less than. Successful if the feature is numerically less than the value.

.. txb:comparison:: le

   Less than or equal. Successful if the feature is numerically less than or equal to the value.

.. txb:comparison:: gt

   Greater than. Successful if feature is numerically greater than the value.

.. txb:comparison:: ge

   Greater than or equal. Successful if the feature is greater than or equal to the value.

.. txb:comparison:: in
   :type: integer, IP address

   Check if the feature is in a range. The value must be a tuple of two values, the minimum and
   the maximum. This matches if the feature is at least the minimum and no more than the maximum.
   The comparison ::

      in: [ 10, 20 ]

   is identical to ::

      all-of:
      -  le: 10
      -  ge: 20

   If the feature is (the Integer) 8, then ::

      in: [ 1, 10 ] # match
      in: [ 9, 20 ] # no match
      in: [ 1, 6 ] # no match
      in: [ 8, 8 ] # match

   For IP Addresses, the value is a range. It can be specified as a string that can be parsed as
   an IP range.

        *  single address - "172.16.23.8"
        *  a CIDR network - "172.16.23.8/29"
        *  two addresses separated by a dash - "172.16.23.8-172.16.23.15"

   *  A single value, repreenting a single value range.

   *  A dash separated pair of IP addresses, representing an inclusive range. These are equivalent ::

         in: "192.168.56.1-192.168.56.99"
         in: [ 192.168.56.1, 192.168.56.99 ]

   *  A CIDR notation network, which is treated a range that contains exactly the network. These are
      equivalent ::

         in: [ 172.16.23.0 , 172.16.23.127 ]
         in: "172.16.23.0-127.16.23.127"
         in: "172.16.23.0/25"

Boolean Comparisons
===================

.. txb:comparison:: is-true

    Matches if the active feature is a boolen that has the value ``true``. The value, if any, is ignored.

.. txb:comparison:: is-false

    Matches if the active feature is a boolen that has the value ``false``. The value, if any, is ignored.

Compound Comparisons
====================

These comparisons do not directly compare values. They combine or change other comparisons.

Combining Comparisons
---------------------

These combine the results of other comparisons.

.. comparison:: any-of

   Given a list of comparisons, this comparison succeeds if *any* of the comparisons in the list
   succeed. This is another term for "or". This stops doing comparisons in the list as soon as one
   succeeds.

.. comparison:: all-of

   Given a list of comparisons, this comparison succeeds if *all* of the comparisons in the list
   succeed. This is another term for "and". This stops doing comparisons in the list as soon as one
   does not succeed.

.. comparison:: none-of

   Given a list of comparisons, this comparison succeeds if *none* of the comparisons in the list
   succeed. This stops as doing comparisons as soon as one succeeds.

   This serves as the "not" comparison if the list is of length 1. For instance, if the goal was to
   set the field "Best-Band" in proxy requests where the "App" field does **not** match a specific
   regular expression ::

      with: proxy-req-field<App>
      select:
      - none-of:
        -  rxp: "^channel=(?:(?:.* metal)|(?:.*symphonic.*))"
        do:
        - proxy-req-field<Best-Band>: "Delain"

   This could be done as a "negative regular expression" but those are tricky to write, slow, and
   can create stack explosions. This approach is more robust and faster. Note the ``do`` is
   attached to the ``none-of``. If attached to ``rxp`` those directives would trigger on the ``rxp``
   succeeding, not on it failing.

Tuple Comparisons
-----------------

These comparisons are compound in that they do not directly compare values but take other
comparisons and apply those. They are intended for use on tuples or lists of values. The "for-..."
comparisons treat the tuple as a homogenous list where the same comparison is used on every element.
:code:`as-tuple` is for heterogenous lists where the different comparisons are used on different
elements of the tuple.

.. comparison:: for-any

   The value must be another comparison. The comparison is applied to every element of the tuple and
   the comparison is successful if nested comparison is successful for any element of the tuple.

.. comparison:: for-all

   The value must be another comparison. The comparison is applied to every element of the tuple and
   the comparison is successful if nested comparison is successful for every element of the tuple.

.. comparison:: for-none

   The value must be another comparison. The comparison is applied to every element of the tuple and
   the comparison is successful if nested comparison is successful for no elements of the tuple.

.. comparison:: as-tuple

   Compare a tuple as a tuple. This requires a list of comparison which are applied to the tuple
   elements in the same order. The list may be a different length than the tuple, in which case
   the excess elements (tuple values or comparisons) are ignored.
