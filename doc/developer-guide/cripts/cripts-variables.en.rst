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

.. _cripts-variables:

Variables
*********

Cripts supports a wide variety of variable types, and they can be used in many places. In
many cases, the variable types are automatically converted to the correct type, but in some
cases you may need to explicitly declare the variable. Most commonly you will declare a
variable using either ``auto`` or ``borrow``. The latter is used when working with a
complex object, like a URL or a header, that is managed by ATS itself.

For ``auto`` variables, these are the most common types:

=================== ==================================================================================
Type                Description
=================== ==================================================================================
``integer``         A 64-bit signed integer.
``float``           A 64-bit floating point number.
``boolean``         A boolean value.
``string``          A string.
``string_view``     A string view.
=================== ==================================================================================

Of these, ``string_view`` is perhaps the least familiar type, but it is the most commonly used
type of strings within ATS itself. As such, they are incredibly efficient to use. Again, most of the
time just declare your variables as ``auto`` and let the compiler figure out the type.

.. _cripts-variables-strings:

Strings
=======

String and ``string_views`` are probably the types you will work with most. These types
have a number of methods that can be used to manipulate the string. Strings here are secretly
wrapped in standard C++ strings, and have all the methods that come with them. As an example, to
get the length of a string, you can use the ``size()`` method:

.. code-block:: cpp

     borrow req  = Cript::Client::Request::Get();

     if (req["Host"].size() > 3) {
         // Do something
     }

Here's a list of methods available on strings:

===================   =============================================================================
Method                Description
===================   =============================================================================
``clear()``           Clear (empty) the string.
``empty()``           Check if the string is empty or not, returns ``true`` or ``false``.
``size()``            Return the length of the string. Also available as ``length()``.
``starts_with()``     Check if the string starts with a given string.
``ends_with()``       Check if the string ends with a given string.
``find()``            Find a string within the string.
``rfind()``           Find a string within the string, starting from the end.
``contains()``        Check if the string contains a given string.
``substr()``          Get a substring of the string, arguments are ``start`` and ``end`` position.
``split()``           Split the string into a list of strings, using a delimiter. Returns a list.
``trim()``            Trim whitespace from the string.
``ltrim()``           Trim whitespace from the left of the string.
``rtrim()``           Trim whitespace from the right of the string.
``remove_prefix()``   Remove a prefix string from the string. This does not return a new string.
``remove_suffix()``   Remove a suffix string from the string. This does not return a new string.
``toFloat()``         Convert the string to a float.
``toBool()``          Convert the string to a boolean.
``toInteger()``       Convert the string to an integer.
``toFloat()``         Convert the string to a float.
===================   =============================================================================

In addition to this, there's a number of *matching* features in Cripts, which can be used together
with strings. These are covered in more detail in the :ref:`cripts-matchers` section. Of course,
regular comparisons such as ``==`` and ``!=`` are also available.

.. note::
   The ``find()`` and ``rfind()`` methods return the position of the string within the string, or
   ``Cript::string_view::npos`` if the string is not found.

.. _cripts-variables-configuration:

Configuration Variable
======================

ATS has a flexible set of configurations, many of which can be accessed and modified using Cripts.
These configurations are, in ATS terms, what's called overridable. They all have a default value,
that is set globally via :file:`records.yaml`, but they are also overridable per transaction.

Cript exposes this via the global ``proxy`` object, which is a map of all the configurations.
Best way to understand this is to look at an example:

.. code-block:: cpp

   auto cache_on = proxy.config.http.cache.http.Get();

   if (cache_on > 0) {
     proxy.config.http.ignore_server_no_cache.Set(1);
   }

This is a pretty artificial example, but shows the name space of these configurations, and how they
match the documented ATS configuration names.

The configurations can also be access via a more advanced API, typically used for embedding existing
control planes with Cripts. This is done using the ``Records`` object, for example:

.. code-block:: cpp

   do_remap() {
     auto http_cache = Cript::Cript::Records("proxy.config.http.cache.http");

     if (AsInteger(http_cache.Get()) > 0) {
       CDebug("HTTP Cache is on");
     }
   }

.. _cripts-variables-control:

Control Variable
================

ATS plugins (and Cripts) have access to a number of control functions that can be used to control
some behavior of ATS. This is exposed via the global ``control`` object. This object has a number of
variables that can be used to control the behavior of ATS.

============================   ====================================================================
Variable                       Description
============================   ====================================================================
``control.cache.request``      Control the cache behavior for the request.
``control.cache.response``     Control the cache behavior for the response.
``control.cache.nostore``      Control the cache behavior for the no-store.
``control.logging``            Turn logging on or off.
``control.intercept``          Turn incepts on or off.
``control.debug``              Enable debugging (rarely used)
``control.remap``              Indicate whether remap matching is required or not.
============================   ====================================================================

All of these are controlled via a boolean value, and can be set to either ``true`` or ``false``,
using the same ``Get()`` and ``Set()`` as for configuration variables. As an example, lets randomly
turn off logging for some percentage of requests:

.. code-block:: cpp

   do_remap() {
     if (Cript::random(1000) > 99) {
       control.logging.Set(false); // 10% log sampling
     }
   }


.. _cripts-misc-versions:

Versions
========

Cripts provides a way to get the version of ATS and Cripts at runtime. The
following global variables are available:

============================   ====================================================================
Variable                       Description
============================   ====================================================================
versions.major                 The major version of ATS.
versions.minor                 The minor version of ATS.
versions.patch                 The patch version of ATS.
============================   ====================================================================

.. _cripts-variables-other:

Other Variables
===============

There are a number of other variables that are available in Cripts. They are generally closely
tied to another object, and are therefore documented in various chapters within here. However,
here's a quick list of some of the more common ones:

- :ref:`cripts-connections-variables`
- :ref:`cripts-connections-tcpinfo-variables`
