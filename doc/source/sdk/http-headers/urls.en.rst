URLs
****

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

API URL functions provide access to URL data stored in marshal buffers.
The URL functions can create, copy, retrieve or delete entire URLs; they
can also retrieve or modify parts of URLs, such as port or scheme
information.

The general form of an Internet URL is:

::

       :::text
       scheme://user:password@host:port/stuff

The URL data structure includes support for two specific types of
internet URLs. HTTP URLs have the form:

::

       :::text
       http://user:password@host:port/path;params?query#fragment

The URL port is stored as integer. All remaining parts of the URL
(scheme, user, etc.) are stored as strings. Traffic Server URL functions
are named according to the portion of the URL on which they operate. For
instance, the function that retrieves the host portion of a URL is named
``TSUrlHostGet``.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several preallocated scheme names.

``TS_URL_SCHEME_FILE``
    "file"
    ``TS_URL_LEN_FILE``

``TS_URL_SCHEME_FTP``
    "ftp"
    ``TS_URL_LEN_FTP``

``TS_URL_SCHEME_GOPHER``
    "gopher"
    ``TS_URL_LEN_GOPHER``

``TS_URL_SCHEME_HTTP``
    "http"
    ``TS_URL_LEN_HTTP``

``TS_URL_SCHEME_HTTPS``
    "https"
    ``TS_URL_LEN_HTTPS``

``TS_URL_SCHEME_MAILTO``
    "mailto"
    ``TS_URL_LEN_MAILTO``

``TS_URL_SCHEME_NEWS``
    "news"
    ``TS_URL_LEN_NEWS``

``TS_URL_SCHEME_NNTP``
    "nntp"
    ``TS_URL_LEN_NNTP``

``TS_URL_SCHEME_PROSPERO``
    "prospero"
    ``TS_URL_LEN_PROSPERO``

``TS_URL_SCHEME_TELNET``
    "telnet"
    ``TS_URL_LEN_TELNET``

``TS_URL_SCHEME_WAIS``
    "wais"
    ``TS_URL_LEN_WAIS``

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

```TSUrlClone`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#acb775f48f1da5f6c5bf32c833a236574>`__
```TSUrlCopy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aa2b8d5f9289a23ab985210914a6301a7>`__
```TSUrlCreate`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ad3518ea3bca6a6f2d899b859c6fbbede>`__
```TSUrlDestroy`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a87559aac42f4f9439399ba2bd32693fa>`__
```TSUrlPrint`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#adec26f5a4afe62b4308dd86f97ae08fd>`__
```TSUrlFtpTypeGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a5cd15d2c288a48b832f0fc096ed6fb80>`__
```TSUrlFtpTypeSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a66df700e23085cabf945e92eb1e22890>`__
```TSUrlHostGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a48e626d9497d4d81c0b9d2781f86066b>`__
```TSUrlHostSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a7e550dac573f5780f7ba39509aa881f3>`__
```TSUrlHttpFragmentGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a316696ad964cb6c6afb7e3028da3ef84>`__
```TSUrlHttpFragmentSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a45f7b216882c4517b92929145adf5424>`__
```TSUrlHttpParamsGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a9395d5d794078c8ec0f17f27dc8d8498>`__
```TSUrlHttpParamsSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a566f2996d36663e3eb8ed4a8fda738c3>`__
```TSUrlHttpQueryGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a74a6c51ea9f472cf29514facbf897785>`__
```TSUrlHttpQuerySet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a9846aaf11accd8c817fff48bfaa784e0>`__
```TSUrlLengthGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a96ded2567985b187c0a8274d76d12c17>`__
```TSUrlParse`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a17f785697773b62b1f5094c06896cac5>`__
```TSUrlPasswordGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a14614f77e0c15b206bab8fd6fdfa7bd1>`__
```TSUrlPasswordSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#abb4d983b9d47ba5a254c2b9dd9ad835e>`__
```TSUrlPathGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#aa064a2d5256d839819d1ec8f252c01a9>`__
```TSUrlPathSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad73274cb5b7e98a64d53f992681110b7>`__
```TSUrlPortGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad99bfb408a46f47c4aa9478fc1b95e0c>`__
```TSUrlPortSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aab07c0482fc3c3aae1b545fb0104e3aa>`__
```TSUrlSchemeGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a24c660b5b46f17b24d7a1cc9aa9a4930>`__
```TSUrlSchemeSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a03b1a806ea8d79806dfff39bfe138934>`__
```TSUrlStringGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a1cda3103d8dd59372609aed6c9c47417>`__
```TSUrlUserGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a3c4d7ffcbbda447c3b665dc857a3226b>`__
```TSUrlUserSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a3175e9c89c2bbea5ed50e2a7f52d7f9f>`__
