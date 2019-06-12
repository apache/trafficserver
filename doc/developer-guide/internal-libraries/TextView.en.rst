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

TextView
*************

Synopsis
========

:code:`#include <ts/TextView.h>`

.. class:: TextView

This class acts as a view in to memory allocated / owned elsewhere. It is in effect a pointer and
should be treated as such (e.g. care must be taken to avoid dangling references by knowing where the
memory really is). The purpose is to provide string manipulation that is fast, efficient, and
non-modifying, particularly when temporary "copies" are needed.


Description
===========

:class:`TextView` is a subclass of :code:`std::string_view` and has all of those methods. In addition it
provides a number of ancillary methods of common string manipulation methods.

A :class:`TextView` should be treated as an enhanced character pointer that both a location and a
size. This is when makes it possible to pass sub strings around without having to make copies or
allocation additional memory. This comes at the cost of keeping track of the actual owner of the
string memory and making sure the :class:`TextView` does not outlive the memory owner, just as with
a normal pointer type. Internal for |TS| any place that passes a :code:`char *` and a size is an
excellent candidate for using a :class:`TextView` as it is more convenient and no more risky than
the existing arguments.

In deciding between :code:`std::string_view` and :class:`TextView` remember that these easily and
cheaply cross convert. In general if the string is treated as a block of data, :code:`std::string_view`
is better. If the contents of the string are to be examined / parsed non-uniformly then
:class:`TextView` is better. For example, if the string is used simply as a key or a hash source,
use :code:`std::string_view`. Or, if the string may contain substrings of interests such as key / value
pairs, then use a :class:`TextView`.

:class:`TextView` provides a variety of methods for manipulating the view as a string. These are
provided as families of overloads differentiated by how characters are compared. There are four
flavors.

* Direct, a pointer to the target character.
* Comparison, an explicit character value to compare.
* Set, a set of characters (described by a :class:`TextView`) which are compared, any one of which matches.
* Predicate, a function that takes a single character argument and returns a bool to indicate a match.

If the latter three are inadequate the first, the direct pointer, can be used after finding the
appropriate character through some other mechanism.

The increment operator for :class:`TextView` shrinks the view by one character from the front
which allows stepping through the view in normal way, although the string view itself should be the
loop condition, not a dereference of it.

.. code-block:: cpp

   TextView v;
   size_t hash = 0;
   for ( ; v ; ++v) hash = hash * 13 + * v;

Because the view acts as a container of characters, this can be done non-destructively.

.. code-block:: cpp

   TextView v;
   size_t hash = 0;
   for (char c : v) hash = hash * 13 + c;

Views are cheap to construct therefore making a copy to use destructively is very inexpensive.

:class:`MemSpan` provides a :code:`find` method that searches for a matching value. The type of this
value can be anything that is fixed sized and supports the equality operator. The view is treated as
an array of the type and searched sequentially for a matching value. The value type is treated as
having no identity and cheap to copy, in the manner of a integral type.

Parsing with TextView
-----------------------

A primary use of :class:`TextView` is to do field oriented parsing. It is easy and fast to split
strings in to fields without modifying the original data. For example, assume that :arg:`value`
contains a null terminated string which is possibly several tokens separated by commas.

.. code-block:: cpp

   #include <ctype.h>
   parse_token(const char* value) {
     TextView v(value); // construct assuming null terminated string.
     while (v) {
       TextView token(v.extractPrefix(','));
       token.trim(&isspace);
       if (token) {
         // process token
       }
     }
   }

If :arg:`value` was ``bob  ,dave, sam`` then :arg:`token` would be successively ``bob``, ``dave``,
``sam``. After `sam` was extracted :arg:`value` would be empty and the loop would exit. :arg:`token`
can be empty in the case of adjacent delimiters or a trailing delimiter. Note that no memory
allocation at all is done because each view is a pointer in to :arg:`value` and there is no need to
put nul characters in the source string meaning no need to duplicate it to prevent permanent
changes.

What if the tokens were key / value pairs, of the form `key=value`? This is can be done as in the following example.

.. code-block:: cpp

   #include <ctype.h>
   parse_token(const char* source) {
     TextView in(source); // construct assuming null terminated string.
     while (in) {
       TextView value(in.extractPrefix(','));
       TextView key(value.trim(&isspace).splitPrefix('=').rtrim(&isspace));
       if (key) {
         // it's a key=value token with key and value set appropriately.
         value.ltrim(&isspace); // clip potential space after '='.
       } else {
         // it's just a single token which is in value.
       }
     }
   }

Nested delimiters are handled by further splitting in a recursive way which, because the original
string is never modified, is straight forward.

History
=======

The first attempt at this functionality was in the TSConfig library in the :code:`ts::Buffer` and
:code:`ts::ConstBuffer` classes. Originally intended just as raw memory views,
:code:`ts::ConstBuffer` in particular was repeated enhanced to provide better support for strings.
The header was eventually moved from :literal:`lib/tsconfig` to :literal:`lib/ts` and was used in in
various part of the |TS| core.

There was then a proposal to make these classes available to plugin writers as they proved handy in
the core. A suggested alternative was `Boost.StringRef
<http://www.boost.org/doc/libs/1_61_0/libs/utility/doc/html/string_ref.html>`_ which provides a
similar functionality using :code:`std::string` as the base of the pre-allocated memory. A version
of the header was ported to |TS| (by stripping all the Boost support and cross includes) but in use
proved to provide little of the functionality available in :code:`ts::ConstBuffer`. If extensive
reworking was required in any case, it seemed better to start from scratch and build just what was
useful in the |TS| context.

The next step was the :code:`TextView` class which turned out reasonably well. It was then
suggested that more support for raw memory (as opposed to memory presumed to contain printable ASCII
data) would be useful. An attempt was made to do this but the differences in arguments, subtle
method differences, and return types made that infeasible. Instead :class:`MemSpan` was split off to
provide a :code:`void*` oriented view. String specific methods were stripped out and a few
non-character based methods added.
