HTTP Headers
************

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
  
.. toctree::

   marshal-buffers.en
   mime-headers.en
   urls.en
   guide-to-trafficserver-http-header-system.en
   guide-to-trafficserver-http-header-system/mime-fields-always-belong-to-an-associated-mime-header.en
   guide-to-trafficserver-http-header-system/release-marshal-buffer-handles.en
   guide-to-trafficserver-http-header-system/duplicate-mime-fields-are-not-coalesced.en
  

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

-  ```TSHttpHdrClone`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#abd410a18e8bc73298302c4ff3ee9b0c6>`__
-  ```TSHttpHdrCopy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a5ff26f3836a74e885113423dfd4d9ed6>`__
-  ```TSHttpHdrCreate`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a8bbd8c2aaf70fb579af4520053fd5e10>`__
-  ```TSHttpHdrDestroy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a500ac4aae8f369221cf3ac2e3ce0d2a0>`__
-  ```TSHttpHdrLengthGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a3afc557e4e99565ab81bf6437b65181b>`__
-  ```TSHttpHdrMethodGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a358627e05506baa5c8270891652ac4d2>`__
-  ```TSHttpHdrMethodSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a32bbcacacbef997e89c04cc3898b0ca4>`__
-  ```TSHttpHdrPrint`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a7c88f30d6325a461fb038e6a117b3731>`__
-  ```TSHttpHdrReasonGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a8b1609e9c8a8a52ebe7762b6109d3bef>`__
-  ```TSHttpHdrReasonLookup`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ab49fded8874b8e3e17cf4395c9832378>`__
-  ```TSHttpHdrReasonSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ab86e5f5e7c0af2092c77327d2e0d3b23>`__
-  ```TSHttpHdrStatusGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ac29d5acc357a0c82c83874f42b1e487b>`__
-  ```TSHttpHdrStatusSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#af34459170ed7f3b002ddd597ae38af12>`__
-  ```TSHttpHdrTypeGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#afc1c6f0a3258c4bc6567805df1db1ca3>`__
-  ```TSHttpHdrTypeSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a86058d8590a665dbf43a529714202d3f>`__
-  ```TSHttpHdrUrlGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#af149d7b5c1b8902363afc0ad658c494e>`__
-  ```TSHttpHdrUrlSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ad935635a3918575fa6cca6843c474cfe>`__
-  ```TSHttpHdrVersionGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a90cc8783f5d0bc159f226079aa0104e4>`__
-  ```TSHttpHdrVersionSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aa2a2c03399cdc8dc39b8756f13e7f189>`__
-  ```TSHttpParserClear`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a7cb1b53b4464dc71287351616d6e7509>`__
-  ```TSHttpParserCreate`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a6075fb4e8fc41eb75d640f258722115b>`__
-  `TSHttpParserDestroy <link/to/doxyge>`__
-  ```TSHttpHdrParseReq`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a64193b3c9ddff8bc434c1cc9332004cc>`__
-  ```TSHttpHdrParseResp`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a03c8a14b6ab2b7896ef0e4005222ecff>`__

