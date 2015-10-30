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

.. _developer-plugins-http-headers:

HTTP Headers
************

.. toctree::
   :maxdepth: 2

   trafficserver-http-header-system.en
   header-functions.en
   mime-headers.en
   marshal-buffers.en
   urls.en

An **HTTP message** consists of the following:

-  HTTP header
-  body
-  trailer

The **HTTP header** consists of:

-  A request or response line

   -  An HTTP **request line** contains a method, URL, and version
   -  A **response line** contains a version, status code, and reason
      phrase

-  A MIME header

A **MIME header** is comprised of zero or more MIME fields. A **MIME
field** is composed of a field name, a colon, and (zero or more) field
values. The values in a field are separated by commas. An HTTP header
containing a request line is usually referred to as a **request**. The
following example shows a typical request header:

.. code-block:: http

   GET http://www.tiggerwigger.com/ HTTP/1.0
   Proxy-Connection: Keep-Alive
   User-Agent: Mozilla/5.0 [en] (X11; I; Linux 2.2.3 i686)
   Host: www.tiggerwigger.com
   Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*
   Accept-Encoding: gzip
   Accept-Language: en
   Accept-Charset: iso-8859-1, *, utf-8

The **response header** for the above request might look like the
following:

.. code-block:: http

   HTTP/1.0 200 OK
   Date: Fri, 13 Nov 2009 06:57:43 GMT
   Content-Location: http://locutus.tiggerwigger.com/index.html
   Etag: "07db14afa76be1:1074"
   Last-Modified: Thu, 05 Nov 2009 20:01:38 GMT
   Content-Length: 7931
   Content-Type: text/html
   Server: Microsoft-IIS/4.0
   Age: 922
   Proxy-Connection: close

The following figure illustrates an HTTP message with an expanded HTTP
header.

**Figure 10.1. HTTP Request/Response and Header Structure**

.. figure:: /static/images/sdk/http_header_struct.jpg
   :alt: HTTP Request/Response and Header Structure

   HTTP Request/Response and Header Structure

The figure below shows example HTTP request and response headers.

**Figure 10.2. Examples of HTTP Request and Response Headers**

.. figure:: /static/images/sdk/http_headers.jpg
   :alt: Examples of HTTP Request and Response Headers

   Examples of HTTP Request and Response Headers

The marshal buffer or ``TSMBuffer`` is a heap data structure that stores
parsed URLs, MIME headers, and HTTP headers. You can allocate new
objects out of marshal buffers and change the values within the marshal
buffer. Whenever you manipulate an object, you must require the handle
to the object (``TSMLoc``) and the marshal buffer containing the object
(``TSMBuffer``).

**Figure 10.3. Marshal Buffers and Header Locations**

.. figure:: /static/images/sdk/marshall_buffers.jpg
   :alt: Marshal Buffers and Header Locations

   Marshal Buffers and Header Locations

The figure above shows the following:

-  The marshal buffer containing the HTTP request, ``reqest_bufp``

-  ``TSMLoc`` location pointer for the HTTP header (``http_hdr_loc``)

-  ``TSMLoc`` location pointer for the request URL (``url_loc``)

-  ``TSMLoc`` location pointers for the MIME header (``mime_hdr_loc``)

-  ``TSMLoc`` location pointers for MIME fields (``fieldi_loc``)

-  ``TSMLoc`` location pointer for the next duplicate MIME field
   (``next_dup_loc``)

The diagram also shows that an HTTP header contains pointers to the URL
location and the MIME header location. You can obtain the URL location
from an HTTP header using the function ``TSHttpHdrUrlGet``. To work with
MIME headers, you can pass either a MIME header location or an HTTP
header location to MIME header functions . If you pass an HTTP header to
a MIME header function, then the system locates the associated MIME
header and executes the MIME header function on the MIME header
location.

