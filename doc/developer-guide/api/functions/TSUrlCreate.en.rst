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

TSUrlCreate
***********

Traffic Server URL object construction API.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSUrlCreate(TSMBuffer bufp, TSMLoc * locp)
.. function:: TSReturnCode TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url, TSMLoc * locp)
.. function:: TSReturnCode TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_url, TSMBuffer src_bufp, TSMLoc src_url)
.. function:: TSParseResult TSUrlParse(TSMBuffer bufp, TSMLoc offset, const char ** start, const char * end)

Description
===========

The URL data structure is a parsed version of a standard internet URL. The
Traffic Server URL API provides access to URL data stored in marshal
buffers. The URL functions can create, copy, retrieve or delete entire URLs,
and retrieve or modify parts of URLs, such as their host, port or scheme
information.

:func:`TSUrlCreate` creates a new URL within the marshal buffer
:arg:`bufp`. Release the resulting handle with a call to
:func:`TSHandleMLocRelease`.

:func:`TSUrlClone` copies the contents of the URL at location :arg:`src_url`
within the marshal buffer :arg:`src_bufp` to a location within the marshal
buffer :arg:`dest_bufp`. Release the returned handle with a call to
:func:`TSHandleMLocRelease`.

:func:`TSUrlCopy` copies the contents of the URL at location :arg:`src_url`
within the marshal buffer :arg:`src_bufp` to the location :arg:`dest_url`
within the marshal buffer dest_bufp. :func:`TSUrlCopy` works correctly even if
:arg:`src_bufp` and :arg:`dest_bufp` point to different marshal buffers. It
is important for the destination URL (its marshal buffer and :type:`TSMLoc`)
to have been created before copying into it.

:func:`TSUrlParse` parses a URL. The :arg:`start` pointer is both an input
and an output parameter and marks the start of the URL to be parsed. After a
successful parse, the :arg:`start` pointer equals the :arg:`end`
pointer. The :arg:`end` pointer must be one byte after the last character you
want to parse. The URL parsing routine assumes that everything between
:arg:`start` and :arg:`end` is part of the URL. It is up to higher level
parsing routines, such as :func:`TSHttpHdrParseReq`, to determine the actual
end of the URL.

Return Values
=============

The :func:`TSUrlParse` function returns a :type:`TSParseResult`, where
:data:`TS_PARSE_ERROR` indicates an error. Success is indicated by one of
:data:`TS_PARSE_DONE` or :data:`TS_PARSE_CONT`. The other APIs all return
a :type:`TSReturnCode`, indicating success (:data:`TS_SUCCESS`) or failure
(:data:`TS_ERROR`) of the operation.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSMBufferCreate(3ts)`,
:manpage:`TSUrlHostGet(3ts)`,
:manpage:`TSUrlHostSet(3ts)`,
:manpage:`TSUrlStringGet(3ts)`,
:manpage:`TSUrlPercentEncode(3ts)`
