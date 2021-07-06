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
``TSHandleMLocRelease``. You do not need to deallocate a ``NULL`` handles.
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
:c:func:`TSMimeHdrCopy`).

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
   .. c:var::  const char* TS_MIME_FIELD_ACCEPT
   .. c:var:: int TS_MIME_LEN_ACCEPT

"Accept-Charset"
   .. c:var::  const char* TS_MIME_FIELD_ACCEPT_CHARSET
   .. c:var:: int TS_MIME_LEN_ACCEPT_CHARSET

"Accept-Encoding"
   .. c:var::  const char* TS_MIME_FIELD_ACCEPT_ENCODING
   .. c:var:: int TS_MIME_LEN_ACCEPT_ENCODING

"Accept-Language"
   .. c:var::  const char* TS_MIME_FIELD_ACCEPT_LANGUAGE
   .. c:var:: int TS_MIME_LEN_ACCEPT_LANGUAGE

"Accept-Ranges"
   .. c:var::  const char* TS_MIME_FIELD_ACCEPT_RANGES
   .. c:var:: int TS_MIME_LEN_ACCEPT_RANGES

"Age"
   .. c:var::  const char* TS_MIME_FIELD_AGE
   .. c:var:: int TS_MIME_LEN_AGE

"Allow"
   .. c:var::  const char* TS_MIME_FIELDALLOW
   .. c:var:: int TS_MIME_LEN_ALLOW

"Approved"
   .. c:var::  const char* TS_MIME_FIELDAPPROVED
   .. c:var:: int TS_MIME_LEN_APPROVED

"Authorization"
   .. c:var::  const char* TS_MIME_FIELDAUTHORIZATION
   .. c:var:: int TS_MIME_LEN_AUTHORIZATION

"Bytes"
   .. c:var::  const char* TS_MIME_FIELDBYTES
   .. c:var:: int TS_MIME_LEN_BYTES

"Cache-Control"
   .. c:var::  const char* TS_MIME_FIELDCACHE_CONTROL
   .. c:var:: int TS_MIME_LEN_CACHE_CONTROL

"Client-ip"
   .. c:var::  const char* TS_MIME_FIELDCLIENT_IP
   .. c:var:: int TS_MIME_LEN_CLIENT_IP

"Connection"
   .. c:var::  const char* TS_MIME_FIELDCONNECTION
   .. c:var:: int TS_MIME_LEN_CONNECTION

"Content-Base"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_BASE
   .. c:var:: int TS_MIME_LEN_CONTENT_BASE

"Content-Encoding"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_ENCODING
   .. c:var:: int TS_MIME_LEN_CONTENT_ENCODING

"Content-Language"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_LANGUAGE
   .. c:var:: int TS_MIME_LEN_CONTENT_LANGUAGE

"Content-Length"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_LENGTH
   .. c:var:: int TS_MIME_LEN_CONTENT_LENGTH

"Content-Location"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_LOCATION
   .. c:var:: int TS_MIME_LEN_CONTENT_LOCATION

"Content-MD5"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_MD5
   .. c:var:: int TS_MIME_LEN_CONTENT_MD5

"Content-Range"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_RANGE
   .. c:var:: int TS_MIME_LEN_CONTENT_RANGE

"Content-Type"
   .. c:var::  const char* TS_MIME_FIELDCONTENT_TYPE
   .. c:var:: int TS_MIME_LEN_CONTENT_TYPE

"Control"
   .. c:var::  const char* TS_MIME_FIELDCONTROL
   .. c:var:: int TS_MIME_LEN_CONTROL

"Cookie"
   .. c:var::  const char* TS_MIME_FIELDCOOKIE
   .. c:var:: int TS_MIME_LEN_COOKIE

"Date"
   .. c:var::  const char* TS_MIME_FIELDDATE
   .. c:var:: int TS_MIME_LEN_DATE

"Distribution"
   .. c:var::  const char* TS_MIME_FIELDDISTRIBUTION
   .. c:var:: int TS_MIME_LEN_DISTRIBUTION

"Etag"
   .. c:var::  const char* TS_MIME_FIELDETAG
   .. c:var:: int TS_MIME_LEN_ETAG

"Expect"
   .. c:var::  const char* TS_MIME_FIELDEXPECT
   .. c:var:: int TS_MIME_LEN_EXPECT

"Expires"
   .. c:var::  const char* TS_MIME_FIELDEXPIRES
   .. c:var:: int TS_MIME_LEN_EXPIRES

"Followup-To"
   .. c:var::  const char* TS_MIME_FIELDFOLLOWUP_TO
   .. c:var:: int TS_MIME_LEN_FOLLOWUP_TO

"From"
   .. c:var::  const char* TS_MIME_FIELDFROM
   .. c:var:: int TS_MIME_LEN_FROM

"Host"
   .. c:var::  const char* TS_MIME_FIELDHOST
   .. c:var:: int TS_MIME_LEN_HOST

"If-Match"
   .. c:var::  const char* TS_MIME_FIELDIF_MATCH
   .. c:var:: int TS_MIME_LEN_IF_MATCH

"If-Modified-Since"
   .. c:var::  const char* TS_MIME_FIELDIF_MODIFIED_SINCE
   .. c:var:: int TS_MIME_LEN_IF_MODIFIED_SINCE

"If-None-Match"
   .. c:var::  const char* TS_MIME_FIELDIF_NONE_MATCH
   .. c:var:: int TS_MIME_LEN_IF_NONE_MATCH

"If-Range"
   .. c:var::  const char* TS_MIME_FIELDIF_RANGE
   .. c:var:: int TS_MIME_LEN_IF_RANGE

"If-Unmodified-Since"
   .. c:var::  const char* TS_MIME_FIELDIF_UNMODIFIED_SINCE
   .. c:var:: int TS_MIME_LEN_IF_UNMODIFIED_SINCE

"Keep-Alive"
   .. c:var::  const char* TS_MIME_FIELDKEEP_ALIVE
   .. c:var:: int TS_MIME_LEN_KEEP_ALIVE

"Keywords"
   .. c:var::  const char* TS_MIME_FIELDKEYWORDS
   .. c:var:: int TS_MIME_LEN_KEYWORDS

"Last-Modified"
   .. c:var::  const char* TS_MIME_FIELDLAST_MODIFIED
   .. c:var:: int TS_MIME_LEN_LAST_MODIFIED

"Lines"
   .. c:var::  const char* TS_MIME_FIELDLINES
   .. c:var:: int TS_MIME_LEN_LINES

"Location"
   .. c:var::  const char* TS_MIME_FIELDLOCATION
   .. c:var:: int TS_MIME_LEN_LOCATION

"Max-Forwards"
   .. c:var::  const char* TS_MIME_FIELDMAX_FORWARDS
   .. c:var:: int TS_MIME_LEN_MAX_FORWARDS

"Message-ID"
   .. c:var::  const char* TS_MIME_FIELDMESSAGE_ID
   .. c:var:: int TS_MIME_LEN_MESSAGE_ID

"Newsgroups"
   .. c:var::  const char* TS_MIME_FIELDNEWSGROUPS
   .. c:var:: int TS_MIME_LEN_NEWSGROUPS

"Organization"
   .. c:var::  const char* TS_MIME_FIELDORGANIZATION
   .. c:var:: int TS_MIME_LEN_ORGANIZATION

"Path"
   .. c:var::  const char* TS_MIME_FIELDPATH
   .. c:var:: int TS_MIME_LEN_PATH

"Pragma"
   .. c:var::  const char* TS_MIME_FIELDPRAGMA
   .. c:var:: int TS_MIME_LEN_PRAGMA

"Proxy-Authenticate"
   .. c:var::  const char* TS_MIME_FIELDPROXY_AUTHENTICATE
   .. c:var:: int TS_MIME_LEN_PROXY_AUTHENTICATE

"Proxy-Authorization"
   .. c:var::  const char* TS_MIME_FIELDPROXY_AUTHORIZATION
   .. c:var:: int TS_MIME_LEN_PROXY_AUTHORIZATION

"Proxy-Connection"
   .. c:var::  const char* TS_MIME_FIELDPROXY_CONNECTION
   .. c:var:: int TS_MIME_LEN_PROXY_CONNECTION

"Public"
   .. c:var::  const char* TS_MIME_FIELDPUBLIC
   .. c:var:: int TS_MIME_LEN_PUBLIC

"Range"
   .. c:var::  const char* TS_MIME_FIELDRANGE
   .. c:var:: int TS_MIME_LEN_RANGE

"References"
   .. c:var::  const char* TS_MIME_FIELDREFERENCES
   .. c:var:: int TS_MIME_LEN_REFERENCES

"Referer"
   .. c:var::  const char* TS_MIME_FIELDREFERER
   .. c:var:: int TS_MIME_LEN_REFERER

"Reply-To"
   .. c:var::  const char* TS_MIME_FIELDREPLY_TO
   .. c:var:: int TS_MIME_LEN_REPLY_TO

"Retry-After"
   .. c:var::  const char* TS_MIME_FIELDRETRY_AFTER
   .. c:var:: int TS_MIME_LEN_RETRY_AFTER

"Sender"
   .. c:var::  const char* TS_MIME_FIELDSENDER
   .. c:var:: int TS_MIME_LEN_SENDER

"Server"
   .. c:var::  const char* TS_MIME_FIELDSERVER
   .. c:var:: int TS_MIME_LEN_SERVER

"Set-Cookie"
   .. c:var::  const char* TS_MIME_FIELDSET_COOKIE
   .. c:var:: int TS_MIME_LEN_SET_COOKIE

"Subject"
   .. c:var::  const char* TS_MIME_FIELDSUBJECT
   .. c:var:: int TS_MIME_LEN_SUBJECTTS_MIME_LEN_SUBJECT

"Summary"
   .. c:var::  const char* TS_MIME_FIELDSUMMARY
   .. c:var:: int TS_MIME_LEN_SUMMARY

"TE"
   .. c:var::  const char* TS_MIME_FIELDTE
   .. c:var:: int TS_MIME_LEN_TE

"Transfer-Encoding"
   .. c:var::  const char* TS_MIME_FIELDTRANSFER_ENCODING
   .. c:var:: int TS_MIME_LEN_TRANSFER_ENCODING

"Upgrade"
   .. c:var::  const char* TS_MIME_FIELDUPGRADE
   .. c:var:: int TS_MIME_LEN_UPGRADE

"User-Agent"
   .. c:var::  const char* TS_MIME_FIELDUSER_AGENT
   .. c:var:: int TS_MIME_LEN_USER_AGENT

"Vary"
   .. c:var::  const char* TS_MIME_FIELDVARY
   .. c:var:: int TS_MIME_LEN_VARY

"Via"
   .. c:var::  const char* TS_MIME_FIELDVIA
   .. c:var:: int TS_MIME_LEN_VIA

"Warning"
   .. c:var::  const char* TS_MIME_FIELDWARNING
   .. c:var:: int TS_MIME_LEN_WARNING

"Www-Authenticate"
   .. c:var::  const char* TS_MIME_FIELDWWW_AUTHENTICATE
   .. c:var:: int TS_MIME_LEN_WWW_AUTHENTICATE

"Xref"
   .. c:var::  const char* TS_MIME_FIELDXREF
   .. c:var:: int TS_MIME_LEN_XREF

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

-  :c:func:`TSMimeHdrFieldAppend`
-  :c:func:`TSMimeHdrFieldClone`
-  :c:func:`TSMimeHdrFieldCopy`
-  :c:func:`TSMimeHdrFieldCopyValues`
-  :c:func:`TSMimeHdrFieldCreate`
-  :c:func:`TSMimeHdrFieldDestroy`
-  :c:func:`TSMimeHdrFieldLengthGet`
-  :c:func:`TSMimeHdrFieldNameGet`
-  :c:func:`TSMimeHdrFieldNameSet`
-  :c:func:`TSMimeHdrFieldNext`
-  :c:func:`TSMimeHdrFieldNextDup`
-  :c:func:`TSMimeHdrFieldValueAppend`
-  :c:func:`TSMimeHdrFieldValueAppend`
-  :c:func:`TSMimeHdrFieldValueDateGet`
-  :c:func:`TSMimeHdrFieldValueDateInsert`
-  :c:func:`TSMimeHdrFieldValueDateSet`
-  :c:func:`TSMimeHdrFieldValueIntGet`
-  :c:func:`TSMimeHdrFieldValueIntSet`
-  :c:func:`TSMimeHdrFieldValueStringGet`
-  :c:func:`TSMimeHdrFieldValueStringInsert`
-  :c:func:`TSMimeHdrFieldValueStringSet`
-  :c:func:`TSMimeHdrFieldValueUintGet`
-  :c:func:`TSMimeHdrFieldValueUintInsert`
-  :c:func:`TSMimeHdrFieldValueUintSet`
-  :c:func:`TSMimeHdrFieldValuesClear`
-  :c:func:`TSMimeHdrFieldValuesCount`
-  :c:func:`TSMimeHdrClone`
-  :c:func:`TSMimeHdrCopy`
-  :c:func:`TSMimeHdrCreate`
-  :c:func:`TSMimeHdrDestroy`
-  :c:func:`TSMimeHdrFieldFind`
-  :c:func:`TSMimeHdrFieldGet`
-  :c:func:`TSMimeHdrFieldRemove`
-  :c:func:`TSMimeHdrFieldsClear`
-  :c:func:`TSMimeHdrFieldsCount`
-  :c:func:`TSMimeHdrLengthGet`
-  :c:func:`TSMimeHdrParse`
-  :c:func:`TSMimeParserClear`
-  :c:func:`TSMimeParserCreate`
-  :c:func:`TSMimeParserDestroy`
-  :c:func:`TSMimeHdrPrint`
