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

.. default-domain:: c

==================
TSUrlPercentEncode
==================

Traffic Server URL percent encoding API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSUrlPercentEncode(TSMBuffer bufp, TSMLoc offset, char* dst, size_t dst_size, size_t* length, const unsigned char* map)
.. function:: TSReturnCode TSStringPercentEncode(const char* str, int str_len, char* dst, size_t dst_size, size_t* length, const unsigned char* map)
.. function:: TSReturnCode TSStringPercentDecode(const char* str, size_t str_len, char* dst, size_t dst_size, size_t* length)

Description
===========

The URL data structure is a parsed version of a standard internet URL. The
Traffic Server URL API provides access to URL data stored in marshal
buffers. The URL functions can create, copy, retrieve or delete entire URLs,
and retrieve or modify parts of URLs, such as their host, port or scheme
information.

:func:`TSUrlPercentEncode` performs percent-encoding of the URL object,
storing the new string in the :data:`dst` buffer. The :data:`length` parameter
will be set to the new (encoded) string length, or :data:`0` if the encoding
failed.  :func:`TSStringPercentEncode` is similar but operates on a string. If
the optional :data:`map` parameter is provided (not :data:`NULL`) , it should
be a map of characters to encode.

:func:`TSStringPercentDecode` perform percent-decoding of the string in the
:data:`str` buffer, writing to the :data:`dst` buffer. The source and
destination can be the same, in which case they overwrite. The decoded string
is always guaranteed to be no longer than the source string.

Return values
=============

All these APIs returns a :type:`TSReturnCode`, indicating success
(:data:`TS_SUCCESS`) or failure (:data:`TS_ERROR`) of the operation.

See also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUrlCreate(3ts)`,
:manpage:`TSUrlHostGet(3ts)`,
:manpage:`TSUrlHostSet(3ts)`,
:manpage:`TSUrlStringGet(3ts)`
