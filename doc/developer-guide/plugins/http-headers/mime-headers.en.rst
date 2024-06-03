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

.. _developer-plugins-http-headers-mime-headers:

MIME Headers
************

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

    MIME-header = *MIME-field
    MIME-field = field-name ":" #field-value
    field-name = *token
    field-value = *token

The MIME header data structure is a parsed version of a standard
Internet MIME header. The MIME header data structure is similar to the
URL data structure (see :doc:`URLs <urls.en>`). The actual data is stored in a
marshal buffer; the MIME header functions operate on a marshal buffer
and a location (``TSMLoc``) within the buffer.

After a call to ``TSMimeHdrFieldDestroy`` or ``TSMimeHdrFieldRemove`` is
made, you must deallocate the ``TSMLoc`` handle with a call to
``TSHandleMLocRelease``. You do not need to deallocate a ``nullptr`` handles.
For example: if you call ``TSMimeHdrFieldValueStringGet`` to get the value of
the content type field and the field does not exist, then it returns
``TS_NULL_MLOC``. In such a case, you wouldn't need to deallocate the handle
with a call to ``TSHandleMLocRelease``.

The location (``TSMLoc``) in the :ref:`MIME header
functions <MimeHeaderFxns>` can be either an HTTP header location or
a MIME header location. If an HTTP header location is passed to these
functions, then the system locates the MIME header associated with that
HTTP header and executes the corresponding MIME header operations
specified by the functions (see the example in the description of
:func:`TSMimeHdrCopy`).

**Note:** MIME headers may contain more than one MIME field with the
same name. Previous versions of Traffic Server joined multiple fields
with the same name into one field with composite values, but this
behavior came at a performance cost and caused compatibility issues with
older clients and servers. Hence, the current version of Traffic Server
does not coalesce duplicate fields. Correctly-behaving plugins should
check for the presence of duplicate fields and iterate over the
duplicate fields by using ``TSMimeHdrFieldNextDup``.

To facilitate fast comparisons and reduce storage size, Traffic Server
defines several pre-allocated field names. These field names correspond
to the field names in HTTP and NNTP headers.

"Accept"
   .. cpp:var::  const char* TS_MIME_FIELD_ACCEPT
   .. cpp:var:: int TS_MIME_LEN_ACCEPT

"Accept-Charset"
   .. cpp:var::  const char* TS_MIME_FIELD_ACCEPT_CHARSET
   .. cpp:var:: int TS_MIME_LEN_ACCEPT_CHARSET

"Accept-Encoding"
   .. cpp:var::  const char* TS_MIME_FIELD_ACCEPT_ENCODING
   .. cpp:var:: int TS_MIME_LEN_ACCEPT_ENCODING

"Accept-Language"
   .. cpp:var::  const char* TS_MIME_FIELD_ACCEPT_LANGUAGE
   .. cpp:var:: int TS_MIME_LEN_ACCEPT_LANGUAGE

"Accept-Ranges"
   .. cpp:var::  const char* TS_MIME_FIELD_ACCEPT_RANGES
   .. cpp:var:: int TS_MIME_LEN_ACCEPT_RANGES

"Age"
   .. cpp:var::  const char* TS_MIME_FIELD_AGE
   .. cpp:var:: int TS_MIME_LEN_AGE

"Allow"
   .. cpp:var::  const char* TS_MIME_FIELDALLOW
   .. cpp:var:: int TS_MIME_LEN_ALLOW

"Approved"
   .. cpp:var::  const char* TS_MIME_FIELDAPPROVED
   .. cpp:var:: int TS_MIME_LEN_APPROVED

"Authorization"
   .. cpp:var::  const char* TS_MIME_FIELDAUTHORIZATION
   .. cpp:var:: int TS_MIME_LEN_AUTHORIZATION

"Bytes"
   .. cpp:var::  const char* TS_MIME_FIELDBYTES
   .. cpp:var:: int TS_MIME_LEN_BYTES

"Cache-Control"
   .. cpp:var::  const char* TS_MIME_FIELDCACHE_CONTROL
   .. cpp:var:: int TS_MIME_LEN_CACHE_CONTROL

"Client-ip"
   .. cpp:var::  const char* TS_MIME_FIELDCLIENT_IP
   .. cpp:var:: int TS_MIME_LEN_CLIENT_IP

"Connection"
   .. cpp:var::  const char* TS_MIME_FIELDCONNECTION
   .. cpp:var:: int TS_MIME_LEN_CONNECTION

"Content-Base"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_BASE
   .. cpp:var:: int TS_MIME_LEN_CONTENT_BASE

"Content-Encoding"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_ENCODING
   .. cpp:var:: int TS_MIME_LEN_CONTENT_ENCODING

"Content-Language"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_LANGUAGE
   .. cpp:var:: int TS_MIME_LEN_CONTENT_LANGUAGE

"Content-Length"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_LENGTH
   .. cpp:var:: int TS_MIME_LEN_CONTENT_LENGTH

"Content-Location"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_LOCATION
   .. cpp:var:: int TS_MIME_LEN_CONTENT_LOCATION

"Content-MD5"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_MD5
   .. cpp:var:: int TS_MIME_LEN_CONTENT_MD5

"Content-Range"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_RANGE
   .. cpp:var:: int TS_MIME_LEN_CONTENT_RANGE

"Content-Type"
   .. cpp:var::  const char* TS_MIME_FIELDCONTENT_TYPE
   .. cpp:var:: int TS_MIME_LEN_CONTENT_TYPE

"Control"
   .. cpp:var::  const char* TS_MIME_FIELDCONTROL
   .. cpp:var:: int TS_MIME_LEN_CONTROL

"Cookie"
   .. cpp:var::  const char* TS_MIME_FIELDCOOKIE
   .. cpp:var:: int TS_MIME_LEN_COOKIE

"Date"
   .. cpp:var::  const char* TS_MIME_FIELDDATE
   .. cpp:var:: int TS_MIME_LEN_DATE

"Distribution"
   .. cpp:var::  const char* TS_MIME_FIELDDISTRIBUTION
   .. cpp:var:: int TS_MIME_LEN_DISTRIBUTION

"Etag"
   .. cpp:var::  const char* TS_MIME_FIELDETAG
   .. cpp:var:: int TS_MIME_LEN_ETAG

"Expect"
   .. cpp:var::  const char* TS_MIME_FIELDEXPECT
   .. cpp:var:: int TS_MIME_LEN_EXPECT

"Expires"
   .. cpp:var::  const char* TS_MIME_FIELDEXPIRES
   .. cpp:var:: int TS_MIME_LEN_EXPIRES

"Followup-To"
   .. cpp:var::  const char* TS_MIME_FIELDFOLLOWUP_TO
   .. cpp:var:: int TS_MIME_LEN_FOLLOWUP_TO

"From"
   .. cpp:var::  const char* TS_MIME_FIELDFROM
   .. cpp:var:: int TS_MIME_LEN_FROM

"Host"
   .. cpp:var::  const char* TS_MIME_FIELDHOST
   .. cpp:var:: int TS_MIME_LEN_HOST

"If-Match"
   .. cpp:var::  const char* TS_MIME_FIELDIF_MATCH
   .. cpp:var:: int TS_MIME_LEN_IF_MATCH

"If-Modified-Since"
   .. cpp:var::  const char* TS_MIME_FIELDIF_MODIFIED_SINCE
   .. cpp:var:: int TS_MIME_LEN_IF_MODIFIED_SINCE

"If-None-Match"
   .. cpp:var::  const char* TS_MIME_FIELDIF_NONE_MATCH
   .. cpp:var:: int TS_MIME_LEN_IF_NONE_MATCH

"If-Range"
   .. cpp:var::  const char* TS_MIME_FIELDIF_RANGE
   .. cpp:var:: int TS_MIME_LEN_IF_RANGE

"If-Unmodified-Since"
   .. cpp:var::  const char* TS_MIME_FIELDIF_UNMODIFIED_SINCE
   .. cpp:var:: int TS_MIME_LEN_IF_UNMODIFIED_SINCE

"Keep-Alive"
   .. cpp:var::  const char* TS_MIME_FIELDKEEP_ALIVE
   .. cpp:var:: int TS_MIME_LEN_KEEP_ALIVE

"Keywords"
   .. cpp:var::  const char* TS_MIME_FIELDKEYWORDS
   .. cpp:var:: int TS_MIME_LEN_KEYWORDS

"Last-Modified"
   .. cpp:var::  const char* TS_MIME_FIELDLAST_MODIFIED
   .. cpp:var:: int TS_MIME_LEN_LAST_MODIFIED

"Lines"
   .. cpp:var::  const char* TS_MIME_FIELDLINES
   .. cpp:var:: int TS_MIME_LEN_LINES

"Location"
   .. cpp:var::  const char* TS_MIME_FIELDLOCATION
   .. cpp:var:: int TS_MIME_LEN_LOCATION

"Max-Forwards"
   .. cpp:var::  const char* TS_MIME_FIELDMAX_FORWARDS
   .. cpp:var:: int TS_MIME_LEN_MAX_FORWARDS

"Message-ID"
   .. cpp:var::  const char* TS_MIME_FIELDMESSAGE_ID
   .. cpp:var:: int TS_MIME_LEN_MESSAGE_ID

"Newsgroups"
   .. cpp:var::  const char* TS_MIME_FIELDNEWSGROUPS
   .. cpp:var:: int TS_MIME_LEN_NEWSGROUPS

"Organization"
   .. cpp:var::  const char* TS_MIME_FIELDORGANIZATION
   .. cpp:var:: int TS_MIME_LEN_ORGANIZATION

"Path"
   .. cpp:var::  const char* TS_MIME_FIELDPATH
   .. cpp:var:: int TS_MIME_LEN_PATH

"Pragma"
   .. cpp:var::  const char* TS_MIME_FIELDPRAGMA
   .. cpp:var:: int TS_MIME_LEN_PRAGMA

"Proxy-Authenticate"
   .. cpp:var::  const char* TS_MIME_FIELDPROXY_AUTHENTICATE
   .. cpp:var:: int TS_MIME_LEN_PROXY_AUTHENTICATE

"Proxy-Authorization"
   .. cpp:var::  const char* TS_MIME_FIELDPROXY_AUTHORIZATION
   .. cpp:var:: int TS_MIME_LEN_PROXY_AUTHORIZATION

"Proxy-Connection"
   .. cpp:var::  const char* TS_MIME_FIELDPROXY_CONNECTION
   .. cpp:var:: int TS_MIME_LEN_PROXY_CONNECTION

"Public"
   .. cpp:var::  const char* TS_MIME_FIELDPUBLIC
   .. cpp:var:: int TS_MIME_LEN_PUBLIC

"Range"
   .. cpp:var::  const char* TS_MIME_FIELDRANGE
   .. cpp:var:: int TS_MIME_LEN_RANGE

"References"
   .. cpp:var::  const char* TS_MIME_FIELDREFERENCES
   .. cpp:var:: int TS_MIME_LEN_REFERENCES

"Referer"
   .. cpp:var::  const char* TS_MIME_FIELDREFERER
   .. cpp:var:: int TS_MIME_LEN_REFERER

"Reply-To"
   .. cpp:var::  const char* TS_MIME_FIELDREPLY_TO
   .. cpp:var:: int TS_MIME_LEN_REPLY_TO

"Retry-After"
   .. cpp:var::  const char* TS_MIME_FIELDRETRY_AFTER
   .. cpp:var:: int TS_MIME_LEN_RETRY_AFTER

"Sender"
   .. cpp:var::  const char* TS_MIME_FIELDSENDER
   .. cpp:var:: int TS_MIME_LEN_SENDER

"Server"
   .. cpp:var::  const char* TS_MIME_FIELDSERVER
   .. cpp:var:: int TS_MIME_LEN_SERVER

"Set-Cookie"
   .. cpp:var::  const char* TS_MIME_FIELDSET_COOKIE
   .. cpp:var:: int TS_MIME_LEN_SET_COOKIE

"Subject"
   .. cpp:var::  const char* TS_MIME_FIELDSUBJECT
   .. cpp:var:: int TS_MIME_LEN_SUBJECTTS_MIME_LEN_SUBJECT

"Summary"
   .. cpp:var::  const char* TS_MIME_FIELDSUMMARY
   .. cpp:var:: int TS_MIME_LEN_SUMMARY

"TE"
   .. cpp:var::  const char* TS_MIME_FIELDTE
   .. cpp:var:: int TS_MIME_LEN_TE

"Transfer-Encoding"
   .. cpp:var::  const char* TS_MIME_FIELDTRANSFER_ENCODING
   .. cpp:var:: int TS_MIME_LEN_TRANSFER_ENCODING

"Upgrade"
   .. cpp:var::  const char* TS_MIME_FIELDUPGRADE
   .. cpp:var:: int TS_MIME_LEN_UPGRADE

"User-Agent"
   .. cpp:var::  const char* TS_MIME_FIELDUSER_AGENT
   .. cpp:var:: int TS_MIME_LEN_USER_AGENT

"Vary"
   .. cpp:var::  const char* TS_MIME_FIELDVARY
   .. cpp:var:: int TS_MIME_LEN_VARY

"Via"
   .. cpp:var::  const char* TS_MIME_FIELDVIA
   .. cpp:var:: int TS_MIME_LEN_VIA

"Warning"
   .. cpp:var::  const char* TS_MIME_FIELDWARNING
   .. cpp:var:: int TS_MIME_LEN_WARNING

"Www-Authenticate"
   .. cpp:var::  const char* TS_MIME_FIELDWWW_AUTHENTICATE
   .. cpp:var:: int TS_MIME_LEN_WWW_AUTHENTICATE

"Xref"
   .. cpp:var::  const char* TS_MIME_FIELDXREF
   .. cpp:var:: int TS_MIME_LEN_XREF

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

.. _MimeHeaderFxns:

The MIME header functions are listed below:

-  :func:`TSMimeHdrFieldAppend`
-  :func:`TSMimeHdrFieldClone`
-  :func:`TSMimeHdrFieldCopy`
-  :func:`TSMimeHdrFieldCopyValues`
-  :func:`TSMimeHdrFieldCreate`
-  :func:`TSMimeHdrFieldDestroy`
-  :func:`TSMimeHdrFieldLengthGet`
-  :func:`TSMimeHdrFieldNameGet`
-  :func:`TSMimeHdrFieldNameSet`
-  :func:`TSMimeHdrFieldNext`
-  :func:`TSMimeHdrFieldNextDup`
-  :func:`TSMimeHdrFieldValueAppend`
-  :func:`TSMimeHdrFieldValueAppend`
-  :func:`TSMimeHdrFieldValueDateGet`
-  :func:`TSMimeHdrFieldValueDateInsert`
-  :func:`TSMimeHdrFieldValueDateSet`
-  :func:`TSMimeHdrFieldValueIntGet`
-  :func:`TSMimeHdrFieldValueIntSet`
-  :func:`TSMimeHdrFieldValueStringGet`
-  :func:`TSMimeHdrFieldValueStringInsert`
-  :func:`TSMimeHdrFieldValueStringSet`
-  :func:`TSMimeHdrFieldValueUintGet`
-  :func:`TSMimeHdrFieldValueUintInsert`
-  :func:`TSMimeHdrFieldValueUintSet`
-  :func:`TSMimeHdrFieldValuesClear`
-  :func:`TSMimeHdrFieldValuesCount`
-  :func:`TSMimeHdrClone`
-  :func:`TSMimeHdrCopy`
-  :func:`TSMimeHdrCreate`
-  :func:`TSMimeHdrDestroy`
-  :func:`TSMimeHdrFieldFind`
-  :func:`TSMimeHdrFieldGet`
-  :func:`TSMimeHdrFieldRemove`
-  :func:`TSMimeHdrFieldsClear`
-  :func:`TSMimeHdrFieldsCount`
-  :func:`TSMimeHdrLengthGet`
-  :func:`TSMimeHdrParse`
-  :func:`TSMimeParserClear`
-  :func:`TSMimeParserCreate`
-  :func:`TSMimeParserDestroy`
-  :func:`TSMimeHdrPrint`
-  :func:`TSMimeHdrStringToWKS`
