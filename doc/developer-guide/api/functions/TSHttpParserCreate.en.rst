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

TSHttpParserCreate
******************

Parse HTTP headers from memory buffers.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSHttpParser TSHttpParserCreate(void)
.. function:: void TSHttpParserClear(TSHttpParser parser)
.. function:: void TSHttpParserDestroy(TSHttpParser parser)
.. function:: TSParseResult TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char ** start, const char * end)
.. function:: TSParseResult TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char ** start, const char * end)

Description
===========

:func:`TSHttpParserCreate` creates an HTTP parser object. The
parser's data structure contains information about the header being
parsed. A single HTTP parser can be used multiple times, though not
simultaneously. Before being used again, the parser must be cleared
by calling :func:`TSHttpParserClear`.

:func:`TSHttpHdrParseReq` parses an HTTP request header. The HTTP
header :arg:`offset` must already be created, and must reside
inside the marshal buffer :arg:`bufp`. The :arg:`start` argument
points to the current position of the string buffer being parsed
and the :arg:`end` argument points to one byte after the end of
the buffer to be parsed. On return, :arg:`start` is modified to
point past the last character parsed.

It is possible to parse an HTTP request header a single byte at a
time using repeated calls to :func:`TSHttpHdrParseReq`. As long as
an error does not occur, the :func:`TSHttpHdrParseReq` function
will consume that single byte and ask for more. :func:`TSHttpHdrParseReq`
should be called after :data:`TS_HTTP_READ_REQUEST_HDR_HOOK`.

:func:`TSHttpHdrParseResp` operates in the same manner as
:func:`TSHttpHdrParseReq` except it parses an HTTP response header.
It should be called after :data:`TS_HTTP_READ_RESPONSE_HDR_HOOK`.

:func:`TSHttpParserClear` clears the specified HTTP parser so it
may be used again.

:func:`TSHttpParserDestroy` destroys the TSHttpParser object pointed
to by :arg:`parser`. The :arg:`parser` pointer must not be NULL.

Return Values
=============

:func:`TSHttpHdrParseReq` and :func:`TSHttpHdrParseResp` both return
a :type:`TSParseResult` value. :data:`TS_PARSE_ERROR` is returned
on error, :data:`TS_PARSE_CONT` is returned if parsing of the header
stopped because the end of the buffer was reached, and
:data:`TS_PARSE_DONE` when a \\r\\n\\r\\n pattern is encountered,
indicating the end of the header.

See Also
========

:manpage:`TSAPI(3ts)`
