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

.. _developer-plugins-http-headers-urls:

URLs
****

API URL functions provide access to URL data stored in marshal buffers.
The URL functions can create, copy, retrieve or delete entire URLs; they
can also retrieve or modify parts of URLs, such as port or scheme
information.

The general form of an Internet URL is:

::

       scheme://user:password@host:port/stuff

The URL data structure includes support for two specific types of
internet URLs. HTTP URLs have the form:

::

       http://user:password@host:port/path;params?query#fragment

The URL port is stored as integer. All remaining parts of the URL
(scheme, user, etc.) are stored as strings. Traffic Server URL functions
are named according to the portion of the URL on which they operate. For
instance, the function that retrieves the host portion of a URL is named
``TSUrlHostGet``.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several preallocated scheme names.

.. c:var:: TS_URL_SCHEME_FILE
   "file"

.. c:var:: TS_URL_LEN_FILE

.. c:var:: TS_URL_SCHEME_FTP
   "ftp"

.. c:var:: TS_URL_LEN_FTP

.. c:var:: TS_URL_SCHEME_GOPHER
   "gopher"

.. c:var:: TS_URL_LEN_GOPHER

.. c:var:: TS_URL_SCHEME_HTTP
   "http"

.. c:var:: TS_URL_LEN_HTTP

.. c:var:: TS_URL_SCHEME_HTTPS
   "https"

.. c:var:: TS_URL_LEN_HTTPS

.. c:var:: TS_URL_SCHEME_MAILTO
   "mailto"

.. c:var:: TS_URL_LEN_MAILTO

.. c:var:: TS_URL_SCHEME_NEWS
   "news"

.. c:var:: TS_URL_LEN_NEWS

.. c:var:: TS_URL_SCHEME_NNTP
   "nntp"

.. c:var:: TS_URL_LEN_NNTP

.. c:var:: TS_URL_SCHEME_PROSPERO
   "prospero"

.. c:var:: TS_URL_LEN_PROSPERO

.. c:var:: TS_URL_SCHEME_TELNET
   "telnet"

.. c:var:: TS_URL_LEN_TELNET

.. c:var:: TS_URL_SCHEME_WAIS
   "wais"

.. c:var:: TS_URL_LEN_WAIS

.. c:var:: TS_URL_SCHEME_WS
   "ws"

.. c:var:: TS_URL_LEN_WS

.. c:var:: TS_URL_SCHEME_WSS
   "wss"

.. c:var:: TS_URL_LEN_WSS

The scheme names above are defined in ``ts.h`` as ``const`` ``char*``
strings. When Traffic Server sets the scheme portion of the URL (or any
portion for that matter), it quickly checks to see if the new value is
one of the known values. If it is, then it stores a pointer into a
global table (instead of storing the known value in the marshal buffer).
The scheme values listed above are also pointers into this table. This
allows simple pointer comparison of the value returned from
``TSUrlSchemeGet`` with one of the values listed above. You should use
the Traffic Server-defined values when referring to one of the known
schemes, since doing so can prevent the possibility of spelling errors.

Traffic Server **URL functions** are listed below:

:c:func:`TSUrlClone`
:c:func:`TSUrlCopy`
:c:func:`TSUrlCreate`
:c:func:`TSUrlPrint`
:c:func:`TSUrlFtpTypeGet`
:c:func:`TSUrlFtpTypeSet`
:c:func:`TSUrlHostGet`
:c:func:`TSUrlHostSet`
:c:func:`TSUrlHttpFragmentGet`
:c:func:`TSUrlHttpFragmentSet`
:c:func:`TSUrlHttpParamsGet`
:c:func:`TSUrlHttpParamsSet`
:c:func:`TSUrlHttpQueryGet`
:c:func:`TSUrlHttpQuerySet`
:c:func:`TSUrlLengthGet`
:c:func:`TSUrlParse`
:c:func:`TSUrlPasswordGet`
:c:func:`TSUrlPasswordSet`
:c:func:`TSUrlPathGet`
:c:func:`TSUrlPathSet`
:c:func:`TSUrlPortGet`
:c:func:`TSUrlPortSet`
:c:func:`TSUrlSchemeGet`
:c:func:`TSUrlSchemeSet`
:c:func:`TSUrlStringGet`
:c:func:`TSUrlUserGet`
:c:func:`TSUrlUserSet`
