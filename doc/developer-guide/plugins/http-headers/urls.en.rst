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
.. default-domain:: cpp

.. _developer-plugins-http-headers-urls:

URLs
****

API URL functions provide access to URL data stored in marshal buffers.
The URL functions can create, copy, retrieve or delete entire URLs; they
can also retrieve or modify parts of URLs, such as port or scheme
information.

The general form of an Internet URL is::

       scheme://user:password@host:port/stuff

The URL data structure includes support for two specific types of
internet URLs. HTTP URLs have the form::

       http://user:password@host:port/path;params?query#fragment

The URL port is stored as integer. All remaining parts of the URL
(scheme, user, etc.) are stored as strings. Traffic Server URL functions
are named according to the portion of the URL on which they operate. For
instance, the function that retrieves the host portion of a URL is named
``TSUrlHostGet``.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several preallocated scheme names.

"file"
   .. var:: char const * TS_URL_SCHEME_FILE
   .. var:: int TS_URL_LEN_FILE

"ftp"
   .. var:: char const * TS_URL_SCHEME_FTP
   .. var:: int TS_URL_LEN_FTP

"gopher"
   .. var:: char const * TS_URL_SCHEME_GOPHER
   .. var:: int TS_URL_LEN_GOPHER

"http"
   .. var:: char const * TS_URL_SCHEME_HTTP
   .. var:: int TS_URL_LEN_HTTP

"https"
   .. var:: char const * TS_URL_SCHEME_HTTPS
   .. var:: int TS_URL_LEN_HTTPS

"mailto"
   .. var:: char const * TS_URL_SCHEME_MAILTO
   .. var:: int TS_URL_LEN_MAILTO

"news"
   .. var:: char const * TS_URL_SCHEME_NEWS
   .. var:: int TS_URL_LEN_NEWS

"nntp"
   .. var:: char const * TS_URL_SCHEME_NNTP
   .. var:: int TS_URL_LEN_NNTP

"prospero"
   .. var:: char const * TS_URL_SCHEME_PROSPERO
   .. var:: int TS_URL_LEN_PROSPERO

"telnet"
   .. var:: char const * TS_URL_SCHEME_TELNET
   .. var:: int TS_URL_LEN_TELNET

"wais"
   .. var:: char const * TS_URL_SCHEME_WAIS
   .. var:: int TS_URL_LEN_WAIS

"ws"
   .. var:: char const * TS_URL_SCHEME_WS
   .. var:: int TS_URL_LEN_WS

"wss"
   .. var:: char const * TS_URL_SCHEME_WSS
   .. var:: int TS_URL_LEN_WSS

The scheme names above are defined in ``apidefs.h``. When Traffic Server sets the scheme portion of
the URL (or any portion for that matter), it quickly checks to see if the new value is one of the
known values. If it is, then it stores a pointer into a global table (instead of storing the known
value in the marshal buffer). The scheme values listed above are also pointers into this table. This
allows simple pointer comparison of the value returned from ``TSUrlSchemeGet`` or
``TSUrlRawSchemeGet`` with one of the values listed above. You should use the Traffic Server-defined
values when referring to one of the known schemes, since doing so can prevent the possibility of
spelling errors.

Traffic Server **URL functions** are listed below:

:func:`TSUrlClone`
:func:`TSUrlCopy`
:func:`TSUrlCreate`
:func:`TSUrlPrint`
:func:`TSUrlFtpTypeGet`
:func:`TSUrlFtpTypeSet`
:func:`TSUrlHostGet`
:func:`TSUrlHostSet`
:func:`TSUrlHttpFragmentGet`
:func:`TSUrlHttpFragmentSet`
:func:`TSUrlHttpParamsGet`
:func:`TSUrlHttpParamsSet`
:func:`TSUrlHttpQueryGet`
:func:`TSUrlHttpQuerySet`
:func:`TSUrlLengthGet`
:func:`TSUrlParse`
:func:`TSUrlPasswordGet`
:func:`TSUrlPasswordSet`
:func:`TSUrlPathGet`
:func:`TSUrlPathSet`
:func:`TSUrlPortGet`
:func:`TSUrlRawPortGet`
:func:`TSUrlPortSet`
:func:`TSUrlSchemeGet`
:func:`TSUrlSchemeSet`
:func:`TSUrlStringGet`
:func:`TSUrlUserGet`
:func:`TSUrlUserSet`
