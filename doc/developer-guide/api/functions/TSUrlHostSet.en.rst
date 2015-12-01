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

.. include:: ../../../common.defs

.. default-domain:: c

TSUrlHostSet
************

Traffic Server URL component manipulation API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSUrlHostSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlSchemeSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlUserSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlPasswordSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlPortSet(TSMBuffer bufp, TSMLoc offset, int port)
.. function:: TSReturnCode TSUrlPathSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlHttpParamsSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)

Description
===========

The URL data structure is a parsed version of a standard internet URL. The
Traffic Server URL API provides access to URL data stored in marshal
buffers. The URL functions can create, copy, retrieve or delete entire URLs,
and retrieve or modify parts of URLs, such as their host, port or scheme
information.

:func:`TSUrlSchemeSet`, :func:`TSUrlUserSet`, :func:`TSUrlPasswordSet`,
:func:`TSUrlHostSet`, :func:`TSUrlHttpParamsSet`, :func:`TSUrlHttpQuerySet`
and :func:`TSUrlHttpFragmentSet` each set the specified portion of the URL
located at offset within the marshal buffer :arg:`bufp` to the string
value. If :arg:`length` is :literal:`-1` then these functions assume that value
is NULL-terminated. Otherwise, the length of the :arg:`string` value is taken
to be the value of :arg:`length`. These functions copy the string to within
:arg:`bufp`, so it can be subsequently modified or deleted.

:func:`TSUrlPortSet` sets the port number portion of the URL located at
:arg:`offset` within the marshal buffer :arg:`bufp` to the value
port. Normal canonicalization based on the URL scheme still applies.

Return Values
=============

All these APIs returns a :type:`TSReturnCode`, indicating success
(:data:`TS_SUCCESS`) or failure (:data:`TS_ERROR`) of the operation.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUrlCreate(3ts)`,
:manpage:`TSUrlHostGet(3ts)`,
:manpage:`TSUrlStringGet(3ts)`,
:manpage:`TSUrlPercentEncode(3ts)`
