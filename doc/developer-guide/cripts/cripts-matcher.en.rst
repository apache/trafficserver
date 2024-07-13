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

.. highlight:: cpp
.. default-domain:: cpp

.. _cripts-matchers:

Matchers
********

Cripts supports most common comparisons, ``==``, ``!=``, etc. for many of the
strings and variables that it provides. However, this is sometimes not adequate
for all use cases, so Cripts provides a way to define custom matchers. There are
currently three types of matchers:

============================   ====================================================================
Matcher                        Description
============================   ====================================================================
``Matcher::Range``             Matching IP addresses against one or many IP ranges.
``Matcher::PCRE``              Matching strings against one or many regular expressions.
``Matcher::List::Method``      Match a request method against a list of methods.
============================   ====================================================================

Often you will declare these ranges once, and then use them over and over again. For this purpose,
Cripts allows ranges to be declared ``static``, which means it can optimize the code around the matches.

Here's an example using the regular expression matcher:

.. code-block:: cpp

   do_remap()
   {
     static Matcher::PCRE pcre({"^/([^/]+)/(.*)$", "^(.*)$"}); // Nonsensical ...

     borrow url = Client::URL::Get();

     if (pcre.Match(url.path)) {
       // Do something with the match
     }
   }

.. note::

   For the IP-range and regular expression matcher, you can specify a single range or regular expression,
   it does not have to be declared as a list with the ``{}`` syntax. For both, the single or list arguments
   are strings within ``""``.

.. _cripts-matchers-functions:

Matcher Functions
=================

All matchers have the following functions:

============================   ====================================================================
Function                       Description
============================   ====================================================================
``Match()``                    Match the given string against the matcher.
``Contains()``                 Another name for ``Match()``
``Add()``                      Add another element to the matcher (can not be used with ``static``)
============================   ====================================================================

.. _cripts-matchers-pcre:

Matcher::PCRE
=============

The PCRE matcher is used to match strings against one or many regular expressions. When a match
is found, a ``Matcher::PCRE::Result`` object is returned. This object has the following functions
to deal with the matched results and the capture groups:

============================   ====================================================================
Function                       Description
============================   ====================================================================
``Matched()``                  A boolean indicating if a regex was matched.
``Count()``                    Returns the number of regex capture groups that are matched.
``MatchIX()``                  Returns the index of the matched regex capture group.
[] Index                       Retrieves the matched string for the given capture group index.
============================   ====================================================================

Lets show an example:

.. code-block:: cpp

   do_remap()
   {
     static Matcher::PCRE allow({"^([a-c][^/]*)/?(.*)", "^([g-h][^/]*)/?(.*)"});

     borrow url = Client::URL::Get();

     auto res = allow.Match(url.path);

     if (res.Matched()) {
       CDebug("Matched: {}", res[1]);
       CDebug("Matched: {}", res[2]);
       // Now do something with these URLs matching these paths
     }
   }
