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

===========
TSUrlCreate
===========

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSUrlCreate(TSMBuffer bufp, TSMLoc * locp)
.. function:: TSReturnCode TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url, TSMLoc * locp)
.. function:: TSReturnCode TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_url, TSMBuffer src_bufp, TSMLoc src_url)
.. function:: void TSUrlPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp)
.. function:: TSParseResult TSUrlParse(TSMBuffer bufp, TSMLoc offset, const char ** start, const char * end)
.. function:: int TSUrlLengthGet(TSMBuffer bufp, TSMLoc offset)
.. function:: char * TSUrlStringGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: const char * TSUrlSchemeGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlSchemeSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlUserGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlUserSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlPasswordGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlPasswordSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlHostGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlHostSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: int TSUrlPortGet(TSMBuffer bufp, TSMLoc offset)
.. function:: TSReturnCode TSUrlPortSet(TSMBuffer bufp, TSMLoc offset, int port)
.. function:: const char * TSUrlPathGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlPathSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlHttpParamsGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlHttpParamsSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: const char * TSUrlHttpFragmentGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: TSReturnCode TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc offset, const char * value, int length)
.. function:: TSReturnCode TSStringPercentEncode(const char * str, int str_len, char * dst, size_t dst_size, size_t * length, const unsigned char * map)
.. function:: TSReturnCode TSUrlPercentEncode(TSMBuffer bufp, TSMLoc offset, char * dst, size_t dst_size, size_t * length, const unsigned char * map)
.. function:: TSReturnCode TSStringPercentDecode(const char * str, size_t str_len, char * dst, size_t dst_size, size_t * length)

Description
===========

The URL data structure is a parsed version of a standard internet URL.
The Traffic Server URL API provides access to URL data stored in marshal
buffers. The URL functions can create, copy, retrieve or delete entire
URLs, and retrieve or modify parts of URLs, such as their port or scheme
information.

:func:`TSUrlCreate` creates a new URL within the marshal buffer bufp. Release
the resulting handle with a call to TSHandleMLocRelease.
:func:`TSUrlClone` copies the contents of the URL at location src_url within
the marshal buffer src_bufp to a location within the marshal buffer
dest_bufp. Release the returned handle with a call to
:func:`TSHandleMLocRelease`.

:func:`TSUrlCopy` copies the contents of the URL at location src_url within
the marshal buffer src_bufp to the location dest_url within the marshal
buffer dest_bufp. :func:`TSUrlCopy` works correctly even if src_bufp and
dest_bufp point to different marshal buffers. It is important for the
destination URL (its marshal buffer and :type:`TSMLoc`) to have been created
before copying into it.

:func:`TSUrlPrint` formats a URL stored in an :type:`TSMBuffer` to an :type:`TSIOBuffer`.

:func:`TSUrlParse` parses a URL. The start pointer is both an input and an output
parameter and marks the start of the URL to be parsed. After a successful
parse, the start pointer equals the end pointer. The end pointer
must be one byte after the last character you want to parse. The URL
parsing routine assumes that everything between start and end is part of
the URL. It is up to higher level parsing routines, such as
:func:`TSHttpHdrParseReq`, to determine the actual end of the URL.

:func:`TSUrlLengthGet` calculates the length of the URL located at offset
within the marshal buffer bufp if it were returned as a string. This
length will be the same as the length returned by :func:`TSUrlStringGet`.

:func:`TSUrlStringGet` constructs a string representation of the URL located at
offset within the marshal buffer bufp. :func:`TSUrlStringGet` stores the
length of the allocated string in the parameter length. This is the same
length that :func:`TSUrlLengthGet` returns. The returned string is allocated by
a call to :func:`TSmalloc` and must be freed by a call to :func:`TSfree`. If length
is NULL then no attempt is made to de-reference it.

:func:`TSUrlSchemeGet`, :func:`TSUrlUserGet`, :func:`TSUrlPasswordGet`, :func:`TSUrlHostGet`,
:func:`TSUrlHttpParamsGet`, :func:`TSUrlHttpQueryGet` and :func:`TSUrlHttpFragmentGet` each
retrieve an internal pointer to the specified portion of the URL from the
marshall buffer bufp. The length of the returned string is placed in
length and a pointer to the URL portion is returned. The returned string
is not guaranteed to be NUL-terminated.

:func:`TSUrlSchemeSet`, :func:`TSUrlUserSet`, :func:`TSUrlPasswordSet`, :func:`TSUrlHostSet`,
:func:`TSUrlHttpParamsSet`, :func:`TSUrlHttpQuerySet` and :func:`TSUrlHttpFragmentSet` each
set the specified portion of the URL located at offset within the marshal
buffer bufp to the string value. If length is -1 then these functions
assume that value is NUL-terminated. Otherwise, the length of the string
value is taken to be length. These functions copy the string to within
bufp, so it can be subsequently modified or deleted.

:func:`TSUrlPortGet` retrieves the port number portion of the URL located at
offset within the marshal buffer bufp, It returns 0 if there is no port
number.

:func:`TSUrlPortSet` sets the port number portion of the URL located at offset
within the marshal buffer bufp to the value port.

:func:`TSStringPercentEncode` performs percent-encoding of the string str,
storing the new string in the dst buffer. The length parameter will be
set to the new (encoded) string length, or 0 if the encoding failed.
TSUrlPercentEncode is similar but operates on a URL object. If the
optional map parameter is provided, it should be a map of characters to
encode.

:func:`TSStringPercentDecode` perform percent-decoding of the string in the
buffer, writing to the dst buffer. The source and destination can be the
same, in which case they overwrite. The decoded string is always guaranteed
to be no longer than the source string.

See also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSMBufferCreate(3ts)`,
:manpage:`TSmalloc(3ts)`
