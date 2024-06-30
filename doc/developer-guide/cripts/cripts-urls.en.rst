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

.. _cripts-urls:

Urls
****

The URL objects are managed and own by the Cripts subsystem, and as such
must always be borrowed. The pattern for this is as follows:

.. code-block:: cpp

  borrow url = Client::URL::Get();

  auto path = url.path;

There are four types of URLs that can be borrowed:

=====================   =============================================================================
URL Object              Description
=====================   =============================================================================
``Client::URL``         The client request URL, as it currently stands (may be modified).
``Pristine:URL``        The pristine client request URL, as it was coming into the ATS server.
``Parent::URL``         The outgoing server URL, as sent to the next server (parent or origin).
``Cache::URL``          The cache URL, which will be used for the cache lookups.
``Remap::From::URL``    The remap ``from`` URL, from :file:`remap.config`.
``Remap::To::URL``      The remap ``to`` URL, from :file:`remap.config`.
=====================   =============================================================================

These URLs all have the same methods and properties, but they are used in different
hooks and have different meanings. The ``Client::URL`` is the most commonly used URL,
which you will also modify in the traditional remapping use case; for example changing
the ``path`` or ``host`` before further processing.

The full URL string can be copied via the ``String()`` method, for example:

.. code-block:: cpp

  borrow url = Client::URL::Get();

  auto full = url.String();

.. _cripts-urls-components:

Components
----------

Every URL object has the following components:

===============   =================================================================================
Component         Description
===============   =================================================================================
``scheme``        The scheme (http, https, etc).
``host``          The host name.
``port``          The port number, this is an integer value.
``path``          The path.
``query``         The query parameters.
``matrix``        The matrix parameters: Note: This is currently treated as a single string.
===============   =================================================================================

.. note::
   The path component of all URLs in ATS do **not** include the leading slash!

These components can be accessed and modified as needed. Both the ``path`` and ``query`` are
strings, and can be manipulated as such. However, they are both also considered list of their
constituent parts, and can be accessed as such. For example, to get the first part of the path,
you can use the following:

.. code-block:: cpp

  borrow url = Client::URL::Get();

  auto path = url.path; // This is the entire path
  auto first = url.path[0]; // This is the first part of the path

The same pattern applies for the query parameters, except they are key-value pairs, instead of
indexed in a list. To get the value of a specific query parameter, you can use the following:

.. code-block:: cpp

  borrow url = Client::URL::Get();

  auto value = url.query["key"]; // This is the value of the key

You can retrieve the size of the ``path`` or ``query`` using the ``Size()`` method, and you can clear
the path or query using the ``Erase()`` method. To summarize the ``path`` and ``query`` components
have the following methods available to them:

=================   ===============================================================================
Method / access     Description
=================   ===============================================================================
Index []            Access a specific part of the path or query.
``Size()``          Get the number of parts of the path or query.
``Erase()``         Clears the component. Also available as ``Clear()``.
``Sort()``          Sorts the query parameters. **Note**: Only for query parameters.
``Flush()``         Flushes any changes. This is rarely used, since Cripts will manage flushing.
=================   ===============================================================================

In addition, the query parameters ``Erase()`` method can take a single key, or a list of keys to
remove specific parameters. It also allows specify a list of keys to keep, and will remove all other
keys. This is useful for filtering out unwanted query parameters.

For example:

.. code-block:: cpp

  borrow url = Client::URL::get();

  url.query.erase("key"); // Removes the key from the query
  url.query.erase({"key1", "key2"}); // Removes both keys from the query
  url.query.erase({"key1", "key2"}, true); // Removes all keys except key1 and key2
  url.query.keep({"key1", "key2"}); // Same as previous
