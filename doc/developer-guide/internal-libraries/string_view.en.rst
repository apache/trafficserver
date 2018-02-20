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

ts::string_view
***************

Synopsis
========

:code:`#include <ts/string_view.h>`

.. class:: string_view

   An internal implementation of `std::string_view
   <https://en.cppreference.com/w/cpp/header/string_view>`__. This was done because
   :code:`std::string_view` is part of the C++17 standard and therefore cannot be assumed in our
   supported compilers (which are currently only C++11).

Description
===========

:class:`string_view` provides a read only view into memory allocated elsewhere and is handy for when
passing around pieces of memory, such as pieces of an HTTP header. It is essentially a pointer and a
length and a quick glance at our internal API will provide numerous places this kind of data is
passed.

This implementation is intended to be as similar as possible to the standard version to avoid
transition difficulties if / when we upgrade to C++17. For this reason no additional features,
regardless of how useful we might find them, have been or will be added to this class.

The only known difference at this time is the literal operator is :code:`""_sv` instead of
:code:`""sv` as it is for :code:`std::string_view`. The reason is a compiler limitation which does
not allow non-compiler headers to define literal operators without a leading ``_``. The use would be

.. code-block:: cpp

   ts::string_view ts_v = "A literal string"_sv; // ts::string_view
   std::string_view std_v = "A literal string"sv; // std::string_view

If you discover any other differences, that is a bug in our implementation and should be fixed.

For a class that provides a much richer set of text manipulation methods, see :class:`TextView`
which is a subclass of :class:`string_view`.

For passing instance of :class:`string_view` it is reasonable to pass it by value. Examining machine
code shows this is the same cost as passing the pointer and length as two arguments and saves
indirection on in the called code.

There is no shortage of additional reference material available, beyond the basic description noted
above, which serves to describe the API and usage of this class, and duplicating it here would serve
no purpose.
