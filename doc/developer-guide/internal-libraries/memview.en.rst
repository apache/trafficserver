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

MemView
*************

Synopsis
========

:code:`#include <ts/MemView.h>`

.. class:: MemView

.. class:: StringView

These classes act as views in to already allocated memory. Internally in |TS| work must be done with
string or memory entities that are embedded in larger pre-existing memory structures. These classes
are designed to make that easier, more efficient, and less error prone.

Description
===========

The term "view" will be used to mean an instance of :class:`MemView` or :class:`StringView`.
Fundamentally both classes do the same thing, maintain a read only view of a contiguous region of
memory. They differ in the methods and return types due to the conflicting requirements of raw
memory operations and string based operations.

A view is constructed by providing a contiguous region of memory, either based on a start pointer
and a length or a pair of pointers in the usual STL half open range style where the view starts at
the first pointer and ends one short of the second pointer. A view can be empty and refer to no
memory (which what default construction yields). A view attempts to act like a normal pointer in
most situations. A view is only somewhat more expensive than a raw pointer but in most cases a count
is needed as well making a view not any more costly than existing code. Any code that already keeps
a pointer and a count is a good candidate for using :class:`MemView` or :class:`StringView`.

:class:`MemView` and :class:`StringView` inter-convert because the difference between them is simply
the API to access the underingly memory in the view, the actual class internal data is identical.

:class:`StringView` provides a variety of methods for manipulating the view as a string. These are provided as families of overloads differentiated by how characters are compared. There are four flavors.

* Direct, a pointer to the target character.
* Comparison, an explicit character value to compare.
* Set, a set of characters (described by a :class:`StringView`) which are compared, any one of which matches.
* Predicate, a function that takes a single character argument and returns a bool to indicate a match.

If the latter three are inadequate the first, the direct pointer, can be used after finding the
appropriate character through some other mechanism.

The increment operator for :class:`StringView` shrinks the view by one character from the front
which allows stepping through the view in normal way, although the string view itself should be the
loop condition, not a dereference of it.

.. code-block:: cpp

   StringView v;
   size_t hash = 0;
   for ( ; v ; ++v) hash = hash * 13 + *v;

Or, because the view acts as a container of characters, this can be done non-destructively.

.. code-block:: cpp

   StringView v;
   size_t hash = 0;
   for (char c : v) hash = hash * 13 + c;

Views are cheap to construct therefore making a copy to use destructively is very inexpensive.

:class:`MemView` provides a :code:`find` method that searches for a matching value. The type of this
value can be anything that is fixed sized and supports the equality operator. The view is treated as
an array of the type and searched sequentially for a matching value. The value type is treated as
having no identity and cheap to copy, in the manner of a integral type.

Parsing with StringView
-----------------------

A primary use of :class:`StringView` is to do field oriented parsing. It is easy and fast to split
strings in to fields without modifying the original data. For example, assume that :arg:`value`
contains a null terminated string which is possibly several tokens separated by commas.

.. code-block:: cpp

   #include <ctype.h>
   parse_token(const char* value) {
     StringView v(value); // construct assuming null terminated string.
     while (v) {
       StringView token(v.extractPrefix(','));
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
     StringView in(source); // construct assuming null terminated string.
     while (in) {
       StringView value(in.extractPrefix(','));
       StringView key(value.trim(&isspace).splitPrefix('=').rtrim(&isspace));
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

The next step was the :code:`StringView` class which turned out reasonably well. It was then
suggested that more support for raw memory (as opposed to memory presumed to contain printable ASCII
data) would be useful. An attempt was made to do this but the differences in arguments, subtle
method differences, and return types made that infeasible. Instead :class:`MemView` was split off to
provide a :code:`void*` oriented view. String specific methods were stripped out and a few
non-character based methods added.
