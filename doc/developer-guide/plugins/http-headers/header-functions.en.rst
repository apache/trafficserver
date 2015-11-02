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

.. _developer-plugins-http-headers-functions:

Header Functions
****************

The Traffic Server API HTTP header functions enable you to work with
HTTP header data stored in marshal buffers.

The HTTP header data structure is a parsed version of the HTTP header
defined in the HTTP protocol specification. An HTTP header is composed
of a request or response line followed by zero or more MIME fields. In
fact, an HTTP header is a subclass of a MIME header; all of the MIME
header routines operate on HTTP headers.

An HTTP **request line** is composed of a method, a URL, and version. A
**response line** is composed of a version, status code, and reason
phrase. See `About HTTP Headers <../http-headers#AboutHTTPHeaders>`__
for additional details and examples.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several pre-allocated method names. These names correspond to
the methods defined in the HTTP 1.1 specification

``TS_HTTP_METHOD_CONNECT``
   "CONNECT"

``TS_HTTP_METHOD_DELETE``
   "DELETE"

``TS_HTTP_METHOD_GE``
   "GET"

``TS_HTTP_METHOD_HEAD``
   "HEAD"

``TS_HTTP_METHOD_ICP_QUERY``
   "ICP\_QUERY"

``TS_HTTP_METHOD_OPTIONS``
   "OPTIONS"

``TS_HTTP_METHOD_POST``
   "POST"

``TS_HTTP_METHOD_PURGE``
   "PURGE"

``TS_HTTP_METHOD_PUT``
   "PUT"

``TS_HTTP_METHOD_TRACE``
   "TRACE"

``TS_HTTP_METHOD_PUSH``
   "PUSH"

Traffic Server also defines several common values that appear in HTTP
headers.

``TS_HTTP_VALUE_BYTES``
   "bytes"

``TS_HTTP_VALUE_CHUNKED``
   "chunked"

``TS_HTTP_VALUE_CLOSE``
   "close"

``TS_HTTP_VALUE_COMPRESS``
   "compress"

``TS_HTTP_VALUE_DEFLATE``
   "deflate"

``TS_HTTP_VALUE_GZIP``
   "gzip"

``TS_HTTP_VALUE_IDENTITY``
   "identity"

``TS_HTTP_VALUE_KEEP_ALIVE``
   "keep-alive"

``TS_HTTP_VALUE_MAX_AGE``
   "max-age"

``TS_HTTP_VALUE_MAX_STALE``
   "max-stale"

``TS_HTTP_VALUE_MIN_FRESH``
   "min-fresh"

``TS_HTTP_VALUE_MUST_REVALIDATE``
   "must-revalidate"

``TS_HTTP_VALUE_NONE``
   "none"

``TS_HTTP_VALUE_NO_CACHE``
   "no-cache"

``TS_HTTP_VALUE_NO_STORE``
   "no-store"

``TS_HTTP_VALUE_NO_TRANSFORM``
   "no-transform"

``TS_HTTP_VALUE_ONLY_IF_CACHED``
   "only-if-cached"

``TS_HTTP_VALUE_PRIVATE``
   "private"

``TS_HTTP_VALUE_PROXY_REVALIDATE``
   "proxy-revalidate"

``TS_HTTP_VALUE_PUBLIC``
   "public"

``TS_HTTP_VALUE_S_MAX_AGE``
   "s-maxage"

The method names and header values above are defined in ``ts.h`` as
``const char*`` strings. When Traffic Server sets a method or a header
value, it checks to make sure that the new value is one of the known
values. If it is, then it stores a pointer into a global table (instead
of storing the known value in the marshal buffer). The method names and
header values listed above are also pointers into this table. This
allows simple pointer comparison of the value returned from
``TSHttpMethodGet`` with one of the values listed above. It is also
recommended that you use the above values when referring to one of the
known schemes, since this removes the possibility of a spelling error.

The **HTTP Header Functions** are listed below:

-  :c:func:`TSHttpHdrClone`
-  :c:func:`TSHttpHdrCopy`
-  :c:func:`TSHttpHdrCreate`
-  :c:func:`TSHttpHdrDestroy`
-  :c:func:`TSHttpHdrLengthGet`
-  :c:func:`TSHttpHdrMethodGet`
-  :c:func:`TSHttpHdrMethodSet`
-  :c:func:`TSHttpHdrPrint`
-  :c:func:`TSHttpHdrReasonGet`
-  :c:func:`TSHttpHdrReasonLookup`
-  :c:func:`TSHttpHdrReasonSet`
-  :c:func:`TSHttpHdrStatusGet`
-  :c:func:`TSHttpHdrStatusSet`
-  :c:func:`TSHttpHdrTypeGet`
-  :c:func:`TSHttpHdrTypeSet`
-  :c:func:`TSHttpHdrUrlGet`
-  :c:func:`TSHttpHdrUrlSet`
-  :c:func:`TSHttpHdrVersionGet`
-  :c:func:`TSHttpHdrVersionSet`
-  :c:func:`TSHttpParserClear`
-  :c:func:`TSHttpParserCreate`
-  :c:func:`TSHttpParserDestroy`
-  :c:func:`TSHttpHdrParseReq`
-  :c:func:`TSHttpHdrParseResp`

