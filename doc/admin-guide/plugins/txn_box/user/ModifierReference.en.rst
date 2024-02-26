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

   Copyright 2020, Verizon Media

.. include:: ../../../../common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _modifier_reference:

*******************
Modifier Reference
*******************

Modifiers are a way to modify or filter features.

Modifiers
*********

.. modifier:: else

   The :mod:`else` modifier leaves the feature unchanged unless it is the :code:`NULL` value or is
   an empty string. In that case, the feature is changed to be the value of the modifier key.

.. modifier:: hash

   Compute the hash of a feature and take the result modulo the value. This modifier requires a
   value that is a positive integer.

.. modifier:: filter

   Filter is intended to operate on lists, although it will work on a single value as if it were a
   list of length 1.

   The purpose of filter is to create a new list based on the input list. The common form of this is
   to remove elements.

   The modifier takes a list of comparisons, each of which can have an attached action as a key. The
   valid action keys are

   pass
      Pass the item through to the new list unchanged.

   drop
      Do not pass the item to the new list, in effect removing it.

   replace
      Add an item to the new list in place of the original element. This key takes a feature expression
      which is the new value to put in the new list.

   It is an error to have more than one action for a comparison.

   If no action is specified with a comparison, a match will ``pass`` the item. If an action has no
   comparison, it always matches. The comparisons are checked in order for each element and the first
   match determines the action for that element. If there are no matches, the item is dropped. This
   makes it easy to remove elements. To remove any "set-cookie" field with the loopback address in it -
   ::

      [ proxy-rsp-field<set-cookie> , { filter: [ { contains: "127.0.0.1", drop: }, { pass: } ] } ]

   or long hand ::

      -  proxy-rsp-field<set-cookie>
      -  filter:
         -  contains: "127.0.0.1"
            drop:
         -  pass:

   It is not permitted to have a :code:`do` key with any of the comparisons.

   See :ref:`filter-guide` for examples of using this modifier.

.. modifier:: join
   :arg: Separator

   Join features in to a string. The value is used as the separator between elements. If used on a
   scalar feature it will simply convert that feature to a string. If used on tuple it will convert
   each tuple element to a string and concentate the result, placing the separator between the
   strings. Nested tuples are placed in brackets and the modifier recursively applied. NULL elements are discared, but empty
   strings are retained.

.. modifier:: concat
   :arg: List of separator, string

   Concatenate a string. This takes a list of two values, the separator and the string. If the
   active feature is a non-empty string, and the string value for the modifier is not empty, the
   latter is appended to the active feature. The separator is appended first if the active feature
   does not already end with the separator. For example ::

      [ pre-remap-path , { concat: [ "/" , "albums" ] } ]

   will add "albums" to the pre-remap path. If that path is "delain" the result is "delain/albums". If the path is "delain/" the result is still "delain/albums".

   A common use is to attach a query string to a URL while avoiding adding "?" if there is no query stirng. E.g. ::

      [ "http://delain.nl/albums/{pre-remap-path}", { concat: [ "?" , pre-remap-query ] } ]

   which propagate the query string without creating a URL ending in "?". If there was no query
   string :ex:`pre-remap-query` will be the empty string and the modifier will not change the
   string.

   This can be used to append separated strings even on empty fields. For instance, to make sure the
   list of rock bands contains "Delain", it would be

   .. literalinclude:: ../../../../../tests/gold_tests/pluginTest/txn_box/basic/mod.replay.yaml
     :start-after: doc-concat-empty-<
     :end-before: doc-concat-empty->

   An empty field is changed to "Delain", while if there is already a value, a comma is added before
   adding "Delain".

.. modifier:: as-bool

   Coerce the feature to a Boolean if possible. The following types can be coerced

   Boolean
      Identity.
   Integer
      0 is false, any other value is true.
   Float
      0.0 is false, anything else is true.
   String
      False unless the string is one of "true", "1", "on", "enable", "Y", "yes".
   IP Address
      A valid IP address is true, otherwise false.
   Duration
      A non-zero duration is true, otherwise false.
   Tuple
      A non-zero length tuple is true, otherwise false. Note the value(s) in the tuple are not
      examined.
   Null
      False.

.. modifier:: as-integer

   Coerce the feature to an Integer type if possible. If the feature is already an Integer, it is
   left unchanged. If it is a string, it is parsed as an integer string and if that is successfull, the
   feature is changed to be the resulting integer value. If not, the modifier value is used. This is the
   :code:`NULL` value if unspecified.

.. modifier:: as-duration

   Convert to a duration.  If the feature is a string the conversion is done based on following table.

   ============ ========================
   Duration     Names
   ============ ========================
   nanoseconds  ns, nanoseconds
   microseconds us, microseconds
   milliseconds ms, milliseconds
   second       s, sec, second, seconds
   minute       m, min, minute, minutes
   hour         h, hour, hours
   day          d, day, days
   week         w, week, weeks
   ============ ========================

   The string must consist of pairs, each pair an integer followed by a name. Spaces are ignored,
   but can be added for clarity. The duration for the string is the sum of all the pairs,
   irrespective of order. For instance, a duration of ninety minutes could be "90 minutes", "90m",
   "1h 30m", "30 m 1 hour", "5400 sec", or even "900 s1 hours15 minute". Note the singular vs.
   plural forms are purely for convenience. "1 day" and "1 days" are indistinguishable, as are "10
   minute" and "10 minutes".

   If used on a tuple, each element will be coerced to a duration and all of the durations summed.

.. modifier:: as-ip-addr

   Convert to an IP address or range. If the conversion can not be done, the result is :code:`NULL`.

.. modifier:: ip-space
   :arg: IP Space name

   This modifier must be used in conjunction with :drtv:`ip-space-define`. The latter defines and
   loads an IP space. This modifier changes an IP address feature into a row from the named IP
   Space. The value for the modifier is a feature expression in which :ex:`ip-col` can be used to
   extract data from the row. The value of that expression replaces the IP address.

.. modifier:: as-text-block
   :arg: Block name as a string.

   Convert a string in to the contents of a text block. The active value is treated as the name.
   The modifier value is used if there is no block for the name. If there is no block and no name
   the result is an empty string.

.. modifier:: url-encode
   :value: string

   The :mod:`url-encode` perform percent-encoding of a feature, this provides a mechanism for encoding information in a
   Uniform Resource Identifier (URI). This modifier uses the |TS| api :code:`TSStringPercentEncode` to perform the encoding.
   In addition we are using a custom map optional in the mentioned API) to deal with reserved characters(RFC2396:Sec 2.2).

   .. code::

      reserved    = ";" | "/" | "?" | ":" | "@" | "&" | "=" | "+" |
                    "$" | ","

   The following example shows how a particular field can be encoded.

   .. literalinclude:: ../../../../../tests/gold_tests/pluginTest/txn_box/basic/mod.replay.yaml
     :start-after: doc-url-encode-<
     :end-before: doc-url-encode->

   Perform a redirect and apply the :mod:`url-encode` to :ex:`pre-remap-query` in combination with :mod:`concat`.

   .. literalinclude:: ../../../../../tests/gold_tests/pluginTest/txn_box/basic/redirect.replay.yaml
     :start-after: doc-redirect-url-encode-form-<
     :end-before: doc-redirect-url-encode-form->

.. modifier:: url-decode
   :value: string

   The :mod:`url-decode` perform percent-decoding of a feature, this provides a mechanism for encoding information in a
   Uniform Resource Identifier (URI).This internally uses  the |TS| api :code:`TSStringPercentDecode` to perform the decoding.


   The following example shows how a particular field can be encoded.

   .. literalinclude:: ../../../../../tests/gold_tests/pluginTest/txn_box/basic/mod.replay.yaml
     :start-after: doc-url-decode-<
     :end-before: doc-url-decode->

   Perform a redirect and apply the :mod:`url-decode` to :ex:`pre-remap-query` in combination with :mod:`concat`.

   .. literalinclude:: ../../../../../tests/gold_tests/pluginTest/txn_box/basic/redirect.replay.yaml
     :start-after: doc-redirect-url-decode-form-<
     :end-before: doc-redirect-url-decode-form->

.. modifier:: query-sort
   :arg: ``nc``, ``rev``

   Sort a query string by name.

   ``nc``
      Caseless comparison for sorting.

   ``rev``
      Sort in reverse (descending) order.


.. modifier:: query-filter

   This is designed to work with URL query strings in a manner similar to :mod:`filter`. The filter
   is applied to each element in the query string. Comparisons match on the name. The basic actions
   are the same as :mod:`filter`.

   pass
      Pass element to the output unchanged.

   drop
      Do not pass the element to the output (the element is removed).

   replace
      Replace the element with a new element. This can have two nested keys, ``name`` and ``value``.
      These are optional and should contain feature expressions which will replace the existing
      text.

   It is an error to have more than one action for a comparison.

   It is an error to have a :code:`do` key with any of the comparisons.

   If no action is specified with a comparison, a match will ``pass`` the item. If an action has no
   comparison, it always matches. The comparisons are checked in order for each element and the first
   match determines the action for that element. If there are no matches, the item is dropped.

   In addition to actions there are a set of options which are attached to the ``option`` key with
   the comparison. These nested keys are

   value
      This must contain a comparison which is applied to the value for the element.

   append
      Append a new element to the output. This requires a list of two elements, the name and value.
      This will be added after the current element (if passed or replaced). It is valid to ``drop``
      the current element and then ``append``, although that is more easily done by using the list
      form of ``replace``.

   append-unique

      **NOT IMPLEMENTED**

      As ``append`` but only once. If this is used, the name for the appended element will appear
      exactly once in the modified query string. Using this will effect all elements with the name,
      regardless of how such elements are added to the output. If some previous element was passed
      with this name, this append will be dropped to satisfy the uniqueness.

   pass-rest
      Stop filtering - all remaining elements are passed.

   drop-rest
      Stop filtering - all remaining elements are dropped.

   For feature expressions in the scope of this modifier the extractors ``name`` and ``value`` are
   locally bound to the name and value of the current element.

   Example - drop all elements with the name "email" if the address is for "base.ex". ::

      query-filter:
         -  match: "email"
            option:
               value:
                  suffix: "@base.ex"
            drop:
         -  pass: # everything else pass on through.

   Change the name "x-email" to "email" for all elements. ::

      query-filter:
         -  match: "x-email"
            replace:
               name: "email"
         -  pass:

   Make every value more exciting. ::

      query-filter:
         - replace:
              value: "{value}!"

.. modifier:: rxp-replace
   :arg: nc, g

   Perform a regular expression based search and replace on a string. This takes a list of two
   elements, the *pattern* and the *replacement*. As is common this modifier searches the value a
   match for the regular expression *pattern*. If found the text matching *pattern* is removed and
   replaced by replacement*. Capture group extractors are available to extract text from the matched
   text.

   PCRE syntax is used for the regular expression.

   The arguments are

   nc
      Case insensitive matching.

   g
      Global - after a match and replacement, the remaining string is searched for further matches.
