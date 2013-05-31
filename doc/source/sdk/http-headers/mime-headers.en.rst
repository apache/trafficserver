MIME Headers
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

The Traffic Server \*\*MIME header functions \*\* enable you to retrieve
and modify information about HTTP MIME fields.

An HTTP request or response consists of a header, body, and trailer. The
**HTTP** **header** contains a request (or response) line and a MIME
header. A **MIME** **header** is composed of zero or more MIME fields. A
**MIME** **field** is composed of a field name, a colon, and zero or
more field values (values in a field are separated by commas).

In the example below: ``Foo`` is the MIME field name, ``bar`` is the
first MIME field value, and ``car`` is the second MIME field value.

::

      :::text
      Foo: bar, car

The following example is an augmented **Backus-Naur Form** (BNF) for the
form of a MIME header - it specifies exactly what was described above. A
**header** consists of zero or more **fields** that contain a name,
separating colon, and zero or more values. A **name** or **value** is
simply a string of tokens that is potentially zero length; a **token**
is any character except certain control characters and separators (such
as colons). For the purpose of retrieving a field, field names are not
case-sensitive; therefore, the field names ``Foo``, ``foo`` and ``fOO``
are all equivalent.

::

        :::text
    MIME-header = *MIME-field
    MIME-field = field-name ":" #field-value
    field-name = *token
    field-value = *token

The MIME header data structure is a parsed version of a standard
Internet MIME header. The MIME header data structure is similar to the
URL data structure (see `URLs <urls>`__). The actual data is stored in a
marshal buffer; the MIME header functions operate on a marshal buffer
and a location (``TSMLoc``) within the buffer.

After a call to ``TSMimeHdrFieldDestroy``, ``TSMimeHdrFieldRemove``, or
``TSUrlDestroy`` is made, you must deallocate the ``TSMLoc`` handle with
a call to ``TSHandleMLocRelease``. You do not need to deallocate a
``NULL`` handles. For example: if you call
``TSMimeHdrFieldValueStringGet`` to get the value of the content type
field and the field does not exist, then it returns ``TS_NULL_MLOC``. In
such a case, you wouldn't need to deallocate the handle with a call to
``TSHandleMLocRelease``.

The location (``TSMLoc``) in the `MIME header
functions <#MimeHeaderFxns>`__ can be either an HTTP header location or
a MIME header location. If an HTTP header location is passed to these
functions, then the system locates the MIME header associated with that
HTTP header and executes the corresponding MIME header operations
specified by the functions (see the example in the description of
```TSMimeHdrCopy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a6e0a392b2e072db8e7f1d795151203b4>`__).

**Note:** MIME headers may contain more than one MIME field with the
same name. Previous versions of Traffic Server joined multiple fields
with the same name into one field with composite values, but this
behavior came at a performance cost and caused compatability issues with
older clients and servers. Hence, the current version of Traffic Server
does not coalesce duplicate fields. Correctly-behaving plugins should
check for the presence of duplicate fields and iterate over the
duplicate fields by using ``TSMimeHdrFieldNextDup``.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several pre-allocated field names. These field names correspond
to the field names in HTTP and NNTP headers.

``TS_MIME_FIELD_ACCEPT``
    "Accept"
    ``TS_MIME_LEN_ACCEPT``

``TS_MIME_FIELD_ACCEPT_CHARSET``
    "Accept-Charset"
    ``TS_MIME_LEN_ACCEPT_CHARSET``

``TS_MIME_FIELD_ACCEPT_ENCODING``
    "Accept-Encoding"
    ``TS_MIME_LEN_ACCEPT_ENCODING``

``TS_MIME_FIELD_ACCEPT_LANGUAGE``
    "Accept-Language"
    ``TS_MIME_LEN_ACCEPT_LANGUAGE``

``TS_MIME_FIELD_ACCEPT_RANGES``
    "Accept-Ranges"
    ``TS_MIME_LEN_ACCEPT_RANGES``

``TS_MIME_FIELD_AGE``
    "Age"
    ``TS_MIME_LEN_AGE``

``TS_MIME_FIELD_ALLOW``
    "Allow"
    ``TS_MIME_LEN_ALLOW``

``TS_MIME_FIELD_APPROVED``
    "Approved"
    ``TS_MIME_LEN_APPROVED``

``TS_MIME_FIELD_AUTHORIZATION``
    "Authorization"
    ``TS_MIME_LEN_AUTHORIZATION``

``TS_MIME_FIELD_BYTES``
    "Bytes"
    ``TS_MIME_LEN_BYTES``

``TS_MIME_FIELD_CACHE_CONTROL``
    "Cache-Control"
    ``TS_MIME_LEN_CACHE_CONTROL``

``TS_MIME_FIELD_CLIENT_IP``
    "Client-ip"
    ``TS_MIME_LEN_CLIENT_IP``

``TS_MIME_FIELD_CONNECTION``
    "Connection"
    ``TS_MIME_LEN_CONNECTION``

``TS_MIME_FIELD_CONTENT_BASE``
    "Content-Base"
    ``TS_MIME_LEN_CONTENT_BASE``

``TS_MIME_FIELD_CONTENT_ENCODING``
    "Content-Encoding"
    ``TS_MIME_LEN_CONTENT_ENCODING``

``TS_MIME_FIELD_CONTENT_LANGUAGE``
    "Content-Language"
    ``TS_MIME_LEN_CONTENT_LANGUAGE``

``TS_MIME_FIELD_CONTENT_LENGTH``
    "Content-Length"
    ``TS_MIME_LEN_CONTENT_LENGTH``

``TS_MIME_FIELD_CONTENT_LOCATION``
    "Content-Location"
    ``TS_MIME_LEN_CONTENT_LOCATION``

``TS_MIME_FIELD_CONTENT_MD5``
    "Content-MD5"
    ``TS_MIME_LEN_CONTENT_MD5``

``TS_MIME_FIELD_CONTENT_RANGE``
    "Content-Range"
    ``TS_MIME_LEN_CONTENT_RANGE``

``TS_MIME_FIELD_CONTENT_TYPE``
    "Content-Type"
    ``TS_MIME_LEN_CONTENT_TYPE``

``TS_MIME_FIELD_CONTROL``
    "Control"
    ``TS_MIME_LEN_CONTROL``

``TS_MIME_FIELD_COOKIE``
    "Cookie"
    ``TS_MIME_LEN_COOKIE``

``TS_MIME_FIELD_DATE``
    "Date"
    ``TS_MIME_LEN_DATE``

``TS_MIME_FIELD_DISTRIBUTION``
    "Distribution"
    ``TS_MIME_LEN_DISTRIBUTION``

``TS_MIME_FIELD_ETAG``
    "Etag"
    ``TS_MIME_LEN_ETAG``

``TS_MIME_FIELD_EXPECT``
    "Expect"
    ``TS_MIME_LEN_EXPECT``

``TS_MIME_FIELD_EXPIRES``
    "Expires"
    ``TS_MIME_LEN_EXPIRES``

``TS_MIME_FIELD_FOLLOWUP_TO``
    "Followup-To"
    ``TS_MIME_LEN_FOLLOWUP_TO``

``TS_MIME_FIELD_FROM``
    "From"
    ``TS_MIME_LEN_FROM``

``TS_MIME_FIELD_HOST``
    "Host"
    ``TS_MIME_LEN_HOST``

``TS_MIME_FIELD_IF_MATCH``
    "If-Match"
    ``TS_MIME_LEN_IF_MATCH``

``TS_MIME_FIELD_IF_MODIFIED_SINCE``
    "If-Modified-Since"
    ``TS_MIME_LEN_IF_MODIFIED_SINCE``

``TS_MIME_FIELD_IF_NONE_MATCH``
    "If-None-Match"
    ``TS_MIME_LEN_IF_NONE_MATCH``

``TS_MIME_FIELD_IF_RANGE``
    "If-Range"
    ``TS_MIME_LEN_IF_RANGE``

``TS_MIME_FIELD_IF_UNMODIFIED_SINCE``
    "If-Unmodified-Since"
    ``TS_MIME_LEN_IF_UNMODIFIED_SINCE``

``TS_MIME_FIELD_KEEP_ALIVE``
    "Keep-Alive"
    ``TS_MIME_LEN_KEEP_ALIVE``

``TS_MIME_FIELD_KEYWORDS``
    "Keywords"
    ``TS_MIME_LEN_KEYWORDS``

``TS_MIME_FIELD_LAST_MODIFIED``
    "Last-Modified"
    ``TS_MIME_LEN_LAST_MODIFIED``

``TS_MIME_FIELD_LINES``
    "Lines"
    ``TS_MIME_LEN_LINES``

``TS_MIME_FIELD_LOCATION``
    "Location"
    ``TS_MIME_LEN_LOCATION``

``TS_MIME_FIELD_MAX_FORWARDS``
    "Max-Forwards"
    ``TS_MIME_LEN_MAX_FORWARDS``

``TS_MIME_FIELD_MESSAGE_ID``
    "Message-ID"
    ``TS_MIME_LEN_MESSAGE_ID``

``TS_MIME_FIELD_NEWSGROUPS``
    "Newsgroups"
    ``TS_MIME_LEN_NEWSGROUPS``

``TS_MIME_FIELD_ORGANIZATION``
    "Organization"
    ``TS_MIME_LEN_ORGANIZATION``

``TS_MIME_FIELD_PATH``
    "Path"
    ``TS_MIME_LEN_PATH``

``TS_MIME_FIELD_PRAGMA``
    "Pragma"
    ``TS_MIME_LEN_PRAGMA``

``TS_MIME_FIELD_PROXY_AUTHENTICATE``
    "Proxy-Authenticate"
    ``TS_MIME_LEN_PROXY_AUTHENTICATE``

``TS_MIME_FIELD_PROXY_AUTHORIZATION``
    "Proxy-Authorization"
    ``TS_MIME_LEN_PROXY_AUTHORIZATION``

``TS_MIME_FIELD_PROXY_CONNECTION``
    "Proxy-Connection"
    ``TS_MIME_LEN_PROXY_CONNECTION``

``TS_MIME_FIELD_PUBLIC``
    "Public"
    ``TS_MIME_LEN_PUBLIC``

``TS_MIME_FIELD_RANGE``
    "Range"
    ``TS_MIME_LEN_RANGE``

``TS_MIME_FIELD_REFERENCES``
    "References"
    ``TS_MIME_LEN_REFERENCES``

``TS_MIME_FIELD_REFERER``
    "Referer"
    ``TS_MIME_LEN_REFERER``

``TS_MIME_FIELD_REPLY_TO``
    "Reply-To"
    ``TS_MIME_LEN_REPLY_TO``

``TS_MIME_FIELD_RETRY_AFTER``
    "Retry-After"
    ``TS_MIME_LEN_RETRY_AFTER``

``TS_MIME_FIELD_SENDER``
    "Sender"
    ``TS_MIME_LEN_SENDER``

``TS_MIME_FIELD_SERVER``
    "Server"
    ``TS_MIME_LEN_SERVER``

``TS_MIME_FIELD_SET_COOKIE``
    "Set-Cookie"
    ``TS_MIME_LEN_SET_COOKIE``

``TS_MIME_FIELD_SUBJECT``
    "Subject"
    ``TS_MIME_LEN_SUBJECTTS_MIME_LEN_SUBJECT``

``TS_MIME_FIELD_SUMMARY``
    "Summary"
    ``TS_MIME_LEN_SUMMARY``

``TS_MIME_FIELD_TE``
    "TE"
    ``TS_MIME_LEN_TE``

``TS_MIME_FIELD_TRANSFER_ENCODING``
    "Transfer-Encoding"
    ``TS_MIME_LEN_TRANSFER_ENCODING``

``TS_MIME_FIELD_UPGRADE``
    "Upgrade"
    ``TS_MIME_LEN_UPGRADE``

``TS_MIME_FIELD_USER_AGENT``
    "User-Agent"
    ``TS_MIME_LEN_USER_AGENT``

``TS_MIME_FIELD_VARY``
    "Vary"
    ``TS_MIME_LEN_VARY``

``TS_MIME_FIELD_VIA``
    "Via"
    ``TS_MIME_LEN_VIA``

``TS_MIME_FIELD_WARNING``
    "Warning"
    ``TS_MIME_LEN_WARNING``

``TS_MIME_FIELD_WWW_AUTHENTICATE``
    "Www-Authenticate"
    ``TS_MIME_LEN_WWW_AUTHENTICATE``

``TS_MIME_FIELD_XREF``
    "Xref"
    ``TS_MIME_LEN_XREF``

The header field names above are defined in ``ts.h`` as ``const char*``
strings. When Traffic Server sets the name portion of a header field (or
any portion for that matter), it quickly checks to see if the new value
is one of the known values. If it is, then Traffic Server stores a
pointer into a global table instead of storing the known value in the
marshal buffer. The header field names listed above are also pointers
into this table, which enables simple pointer comparison of the value
returned from ``TSMimeHdrFieldNameGet`` with one of the values listed
above. It is recommended that you use the above values when referring to
one of the known header field names to avoid the possibility of a
spelling error.

Traffic Server adds one important feature to MIME fields that you may
not know about: Traffic Server does not print a MIME field if the field
name begins with the '``@``\ ' symbol. For example: a plugin can add the
field "``@My-Field``\ " to a header. Even though Traffic Server never
sends that field out in a request to an origin server or in a response
to a client, it can be printed to Traffic Server logs by defining a
custom log configuration file that explicitly logs such fields. This
provides a useful mechanism for plugins to store information about an
object in one of the MIME headers associated with the object.

The MIME header functions are listed below:

-  ```TSMimeHdrFieldAppend`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ae36c9bab9147a30b259d8e0223d697f2>`__
-  ```TSMimeHdrFieldClone`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ade66cd69ddff40d14b015a9e2cd7b46f>`__
-  ```TSMimeHdrFieldCopy`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a612ebefde403abc216af99f9150dd66f>`__
-  ```TSMimeHdrFieldCopyValues`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a5e4b2f68392a26643620641e50e5045b>`__
-  ```TSMimeHdrFieldCreate`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a7f79c0bea2ce14ced3b017eac08f8916>`__
-  ```TSMimeHdrFieldDestroy`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a6bf2d8e95e6f3ef351f63dbe8bc54020>`__
-  ```TSMimeHdrFieldLengthGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a8a906f22ccf7a4a04fac817dc57a785f>`__
-  ```TSMimeHdrFieldNameGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ad68f51073e4630ad6a0433efbfeef2ea>`__
-  ```TSMimeHdrFieldNameSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a6856f6069fa4ee67d1a788bd642d59f0>`__
-  ```TSMimeHdrFieldNext`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#aaf3a205e8a4e7128f3fa3de70991df80>`__
-  ```TSMimeHdrFieldNextDup`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#af2d776615afe959ed7c3639830a7061f>`__
-  ```TSMimeHdrFieldValueAppend`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad1d4d1dda95311e3389245fd9fa961b5>`__
-  ```TSMimeHdrFieldValueAppend`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad1d4d1dda95311e3389245fd9fa961b5>`__
-  ```TSMimeHdrFieldValueDateGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad74a60f0da93397ee015d82f30021d15>`__
-  ```TSMimeHdrFieldValueDateInsert`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a0520a29de96736b04f14e9d790ec8e9c>`__
-  ```TSMimeHdrFieldValueDateSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#abf85e014cb316977dedca38c341d4369>`__
-  ```TSMimeHdrFieldValueIntGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ace1fac92d2be75ff7cbd8eb7725d3fac>`__
-  ```TSMimeHdrFieldValueIntSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aec96c5629a750cdaec709228c4bd8a76>`__
-  ```TSMimeHdrFieldValueStringGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a4aa55cd4eeb9e6d0a5151c02f0c18c28>`__
-  ```TSMimeHdrFieldValueStringInsert`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a979d1591becf1c59de830af117d54923>`__
-  ```TSMimeHdrFieldValueStringSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ac21e44e84b25c23e52ba7bea7bd09ed6>`__
-  ```TSMimeHdrFieldValueUintGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a116b4c9144ad6eda66213adb0167706a>`__
-  ```TSMimeHdrFieldValueUintInsert`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a88db3a90d3ac7766e55c734c89dfe86f>`__
-  ```TSMimeHdrFieldValueUintSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a63b0a686b4a6ec6b8a4f1b796009c3cd>`__
-  ```TSMimeHdrFieldValuesClear`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a8fd3afaf88f6c76793fdb635bbd22113>`__
-  ```TSMimeHdrFieldValuesCount`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a326283521986acf9b8a9ec00f3d6d164>`__
-  ```TSMimeHdrClone`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aa8ab95bda93c3e16e6d134fe35acd1b6>`__
-  ```TSMimeHdrCopy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a6e0a392b2e072db8e7f1d795151203b4>`__
-  ```TSMimeHdrCreate`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a3427dfbd6b79c531fcba4e8c8b4e217d>`__
-  ```TSMimeHdrDestroy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a60ad7f4f4f9e2559dcc2ff28ebe8d96c>`__
-  ```TSMimeHdrFieldFind`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a30e4ce224761b273a119dcd57f5a352b>`__
-  ```TSMimeHdrFieldGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a31c0c307010a5d19d027ffb3a2656745>`__
-  ```TSMimeHdrFieldRemove`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a96d9a50d0687456e6e67eb2e9a9c2d72>`__
-  ```TSMimeHdrFieldsClear`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a65d7539e48c9f5c26075344dee6c6ae2>`__
-  ```TSMimeHdrFieldsCount`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ab02f7d0cba75cf0146c6a9b507c79fcf>`__
-  ```TSMimeHdrLengthGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a001cd786282f5c9d04189ddf7c96e269>`__
-  ```TSMimeHdrParse`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a4a6042bcd5b5b0a21267c03cf102e90d>`__
-  ```TSMimeParserClear`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ac173b595659d1909aae5410ecd1ce028>`__
-  ```TSMimeParserCreate`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a518072dc5a90b753df7726878119506b>`__
-  ```TSMimeParserDestroy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a5f287f5016d931842c0a5012c3d227b7>`__
-  ```TSMimeHdrPrint`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#adfca8722edc6469df4410b8050406bb0>`__

