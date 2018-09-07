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

.. default-domain:: cpp

Lexicon
*******

Synopsis
========

:code:`#include <ts/Lexicon.h>`

:class:`Lexicon` is a template class designed to facilitate conversions between enumerations and
strings. Each enumeration can have a **primary** name and an arbitrary number of **secondary**
names. Enumerations convert to the primary name, and all the names convert to the enumeration.
Defaults can also be set such that a conversion for a name or enumeration that isn't defined yields
the default. All comparisons are case insensitive.

Description
===========

A :class:`Lexicon` is a template class with a single type, which should be a numeric type, usually
an enumeration. An instance contains a set of **definitions**, each of which is an association
between a value, a primary name, and secondary names, the last of which is optional. All names and
values must be unique across the :class:`Lexicon`. The array operator is used to do the conversions.
When indexed by a value, the primary name for that value is returned. When indexed by a name, the
primary name or any secondary name will yield the same value.

Defaults can be set so that any name or value that does not match a definition yields the default. A
default can set for names or values independently. A default can be explicit or it can be a function
which is called when the :class:`Lexicon` is indexed by a non-matching index. The handler function
must return a default of the appropriate type. It acts as an internal catch for undefined
conversions and is generally used to log the failure while returning a default. It could be used to
compute a default but in the case of names, this is problematic due to memory ownership and thread
safety issues. Because the return type of a name is :code:`std::string_view` there is no signal to
cleanup any allocated memory, and storing the name in the :class:`Lexicon` instance makes it
non-thread safe on read access [#]_.

Definitions can be added by the :func:`Lexicon::define` method. Usually a :class:`Lexicon` will be
constructed with the definitions. Two types of such construction are supported, both of them taking
an initializer list of definitions. The definitions may be a pair of enumeration and primary name,
or the definitions may be an enumeration and an initializer list of names, the first of which is the
primary name. Because initializer lists must be homogenous, all definitions must be of the same
type.

When initialized and defaults set, a :class:`Lexicon` makes it very easy to convert between
enumeration values and strings for debugging and configuration handling, particularly if the
enumeration has a value for invalid. Checking input strings is then simply indexing the
:class:`Lexicon` with the string - if it's valid the appropriate enumeration value is returned,
otherwise the invalid value is returned as the default.

Construction is normally done by initializer lists, as these are easier to work with. There are
two forms, either pairs of value, name or pairs of value, list-of-names. The former is simpler
and sufficient if only primary names are to be defined, but the latter is required if secondary
names are present. In addition similar construction using :code:`std::array` is provided. This
is a bit clunkier, but does enable compile time verification that all values are defined.

Examples
========

Assume the enumeration is

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 30

A :class:`Lexicon` for this could be defined as

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 32

An instance could be constructed, with primary and secondary names, as

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 36-40

Note there are no secondary names for ``INVALID`` but the list form must be used. If no secondary
names are needed, it could be done this way

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 42-46

Assuming the first case with secondary names is used, it would be helpful to set defaults so that
undefined names or values map to the invalid case.

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 48

With this initialization, these checks all succeed

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 50-59

A bit more complex initialization can be used in cases where verifying all of the values in an
enumeration are covered by the :class:`Lexicon`. There is a special constructor for this that
takes an extra argument, :code:`Require<>`. The API is designed with the presumption there is a "LAST_VALUE"
in the enumeration which can be used for size. For example, something like

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 62

with the :class:`Lexicon` specialization as

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 63

To cover everything (except the last value, which would normally be handled by the default), the
initialization would be

.. literalinclude:: ../../../lib/ts/unit-tests/test_Lexicon.cc
   :lines: 64-68

If there is am missing value, this fails to compile with a "ts::Lexicon<Radio>::Definition::value’
is uninitialized reference".

Reference
=========

.. class:: template < typename T > Lexicon

   A bidirectional converter between enumerations of :code:`T` and strings.

   .. function:: T operator [] (std::string_view name)

      Return the enumeration associated with :arg:`name`. If :arg:`name` is not in a definition and
      no default has been set, :code:`std::domain_error` is thrown.

   .. function:: std::string_view operator [] (T value)

      Return the primary name associated with :arg:`value`. If :arg:`value` is not in a definition
      and no default has been set, :code:`std::domain_error` is thrown.

   .. function:: Lexicon & define(T value, std::string_view primary, ... )

      Add a definition. The :arg:`value` is associated with the :arg:`primary` name. An arbitrary
      number of additional secondary names may be provided.

Appendix
========

.. rubric:: Footnotes

.. [#]

   The original implementation predated `std::string_view` and so returned `std::string`. While this
   generation of names for unmatched values easy, it did have a performance cost. In this version I
   chose to use :code:`std::string_view` which makes the default handler functions not as useful,
   but still, in my view, sufficiently useful (for logging at least) to be worth supporting.
