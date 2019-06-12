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

.. c:var:: const char* TS_MIME_FIELD_ACCEPT
  "Accept"

.. c:var:: int TS_MIME_LEN_ACCEPT

.. c:var:: const char* TS_MIME_FIELD_ACCEPT_CHARSET
  "Accept-Charset"

.. c:var:: int TS_MIME_LEN_ACCEPT_CHARSET

.. c:var:: const char* TS_MIME_FIELD_ACCEPT_ENCODING
  "Accept-Encoding"

.. c:var:: int TS_MIME_LEN_ACCEPT_ENCODING

.. c:var:: const char* TS_MIME_FIELD_ACCEPT_LANGUAGE
  "Accept-Language"

.. c:var:: int TS_MIME_LEN_ACCEPT_LANGUAGE

.. c:var:: const char* TS_MIME_FIELD_ACCEPT_RANGES
  "Accept-Ranges"

.. c:var:: int TS_MIME_LEN_ACCEPT_RANGES

.. c:var:: const char* TS_MIME_FIELD_AGE
  "Age"

.. c:var:: int TS_MIME_LEN_AGE

.. c:var:: const char* TS_MIME_FIELDALLOW
  "Allow"

.. c:var:: int TS_MIME_LEN_ALLOW

.. c:var:: const char* TS_MIME_FIELDAPPROVED
  "Approved"

.. c:var:: int TS_MIME_LEN_APPROVED

.. c:var:: const char* TS_MIME_FIELDAUTHORIZATION
  "Authorization"

.. c:var:: int TS_MIME_LEN_AUTHORIZATION

.. c:var:: const char* TS_MIME_FIELDBYTES
  "Bytes"

.. c:var:: int TS_MIME_LEN_BYTES

.. c:var:: const char* TS_MIME_FIELDCACHE_CONTROL
  "Cache-Control"

.. c:var:: int TS_MIME_LEN_CACHE_CONTROL

.. c:var:: const char* TS_MIME_FIELDCLIENT_IP
  "Client-ip"

.. c:var:: int TS_MIME_LEN_CLIENT_IP

.. c:var:: const char* TS_MIME_FIELDCONNECTION
  "Connection"

.. c:var:: int TS_MIME_LEN_CONNECTION

.. c:var:: const char* TS_MIME_FIELDCONTENT_BASE
  "Content-Base"

.. c:var:: int TS_MIME_LEN_CONTENT_BASE

.. c:var:: const char* TS_MIME_FIELDCONTENT_ENCODING
  "Content-Encoding"

.. c:var:: int TS_MIME_LEN_CONTENT_ENCODING

.. c:var:: const char* TS_MIME_FIELDCONTENT_LANGUAGE
  "Content-Language"

.. c:var:: int TS_MIME_LEN_CONTENT_LANGUAGE

.. c:var:: const char* TS_MIME_FIELDCONTENT_LENGTH
  "Content-Length"

.. c:var:: int TS_MIME_LEN_CONTENT_LENGTH

.. c:var:: const char* TS_MIME_FIELDCONTENT_LOCATION
  "Content-Location"

.. c:var:: int TS_MIME_LEN_CONTENT_LOCATION

.. c:var:: const char* TS_MIME_FIELDCONTENT_MD5
  "Content-MD5"

.. c:var:: int TS_MIME_LEN_CONTENT_MD5

.. c:var:: const char* TS_MIME_FIELDCONTENT_RANGE
  "Content-Range"

.. c:var:: int TS_MIME_LEN_CONTENT_RANGE

.. c:var:: const char* TS_MIME_FIELDCONTENT_TYPE
  "Content-Type"

.. c:var:: int TS_MIME_LEN_CONTENT_TYPE

.. c:var:: const char* TS_MIME_FIELDCONTROL
  "Control"

.. c:var:: int TS_MIME_LEN_CONTROL

.. c:var:: const char* TS_MIME_FIELDCOOKIE
  "Cookie"

.. c:var:: int TS_MIME_LEN_COOKIE

.. c:var:: const char* TS_MIME_FIELDDATE
  "Date"

.. c:var:: int TS_MIME_LEN_DATE

.. c:var:: const char* TS_MIME_FIELDDISTRIBUTION
  "Distribution"

.. c:var:: int TS_MIME_LEN_DISTRIBUTION

.. c:var:: const char* TS_MIME_FIELDETAG
  "Etag"

.. c:var:: int TS_MIME_LEN_ETAG

.. c:var:: const char* TS_MIME_FIELDEXPECT
  "Expect"

.. c:var:: int TS_MIME_LEN_EXPECT

.. c:var:: const char* TS_MIME_FIELDEXPIRES
  "Expires"

.. c:var:: int TS_MIME_LEN_EXPIRES

.. c:var:: const char* TS_MIME_FIELDFOLLOWUP_TO
  "Followup-To"

.. c:var:: int TS_MIME_LEN_FOLLOWUP_TO

.. c:var:: const char* TS_MIME_FIELDFROM
  "From"

.. c:var:: int TS_MIME_LEN_FROM

.. c:var:: const char* TS_MIME_FIELDHOST
  "Host"

.. c:var:: int TS_MIME_LEN_HOST

.. c:var:: const char* TS_MIME_FIELDIF_MATCH
  "If-Match"

.. c:var:: int TS_MIME_LEN_IF_MATCH

.. c:var:: const char* TS_MIME_FIELDIF_MODIFIED_SINCE
  "If-Modified-Since"

.. c:var:: int TS_MIME_LEN_IF_MODIFIED_SINCE

.. c:var:: const char* TS_MIME_FIELDIF_NONE_MATCH
  "If-None-Match"

.. c:var:: int TS_MIME_LEN_IF_NONE_MATCH

.. c:var:: const char* TS_MIME_FIELDIF_RANGE
  "If-Range"

.. c:var:: int TS_MIME_LEN_IF_RANGE

.. c:var:: const char* TS_MIME_FIELDIF_UNMODIFIED_SINCE
  "If-Unmodified-Since"

.. c:var:: int TS_MIME_LEN_IF_UNMODIFIED_SINCE

.. c:var:: const char* TS_MIME_FIELDKEEP_ALIVE
  "Keep-Alive"

.. c:var:: int TS_MIME_LEN_KEEP_ALIVE

.. c:var:: const char* TS_MIME_FIELDKEYWORDS
  "Keywords"

.. c:var:: int TS_MIME_LEN_KEYWORDS

.. c:var:: const char* TS_MIME_FIELDLAST_MODIFIED
  "Last-Modified"

.. c:var:: int TS_MIME_LEN_LAST_MODIFIED

.. c:var:: const char* TS_MIME_FIELDLINES
  "Lines"

.. c:var:: int TS_MIME_LEN_LINES

.. c:var:: const char* TS_MIME_FIELDLOCATION
  "Location"

.. c:var:: int TS_MIME_LEN_LOCATION

.. c:var:: const char* TS_MIME_FIELDMAX_FORWARDS
  "Max-Forwards"

.. c:var:: int TS_MIME_LEN_MAX_FORWARDS

.. c:var:: const char* TS_MIME_FIELDMESSAGE_ID
  "Message-ID"

.. c:var:: int TS_MIME_LEN_MESSAGE_ID

.. c:var:: const char* TS_MIME_FIELDNEWSGROUPS
  "Newsgroups"

.. c:var:: int TS_MIME_LEN_NEWSGROUPS

.. c:var:: const char* TS_MIME_FIELDORGANIZATION
  "Organization"

.. c:var:: int TS_MIME_LEN_ORGANIZATION

.. c:var:: const char* TS_MIME_FIELDPATH
  "Path"

.. c:var:: int TS_MIME_LEN_PATH

.. c:var:: const char* TS_MIME_FIELDPRAGMA
  "Pragma"

.. c:var:: int TS_MIME_LEN_PRAGMA

.. c:var:: const char* TS_MIME_FIELDPROXY_AUTHENTICATE
  "Proxy-Authenticate"

.. c:var:: int TS_MIME_LEN_PROXY_AUTHENTICATE

.. c:var:: const char* TS_MIME_FIELDPROXY_AUTHORIZATION
  "Proxy-Authorization"

.. c:var:: int TS_MIME_LEN_PROXY_AUTHORIZATION

.. c:var:: const char* TS_MIME_FIELDPROXY_CONNECTION
  "Proxy-Connection"

.. c:var:: int TS_MIME_LEN_PROXY_CONNECTION

.. c:var:: const char* TS_MIME_FIELDPUBLIC
  "Public"

.. c:var:: int TS_MIME_LEN_PUBLIC

.. c:var:: const char* TS_MIME_FIELDRANGE
  "Range"

.. c:var:: int TS_MIME_LEN_RANGE

.. c:var:: const char* TS_MIME_FIELDREFERENCES
  "References"

.. c:var:: int TS_MIME_LEN_REFERENCES

.. c:var:: const char* TS_MIME_FIELDREFERER
  "Referer"

.. c:var:: int TS_MIME_LEN_REFERER

.. c:var:: const char* TS_MIME_FIELDREPLY_TO
  "Reply-To"

.. c:var:: int TS_MIME_LEN_REPLY_TO

.. c:var:: const char* TS_MIME_FIELDRETRY_AFTER
  "Retry-After"

.. c:var:: int TS_MIME_LEN_RETRY_AFTER

.. c:var:: const char* TS_MIME_FIELDSENDER
  "Sender"

.. c:var:: int TS_MIME_LEN_SENDER

.. c:var:: const char* TS_MIME_FIELDSERVER
  "Server"

.. c:var:: int TS_MIME_LEN_SERVER

.. c:var:: const char* TS_MIME_FIELDSET_COOKIE
  "Set-Cookie"

.. c:var:: int TS_MIME_LEN_SET_COOKIE

.. c:var:: const char* TS_MIME_FIELDSUBJECT
  "Subject"

.. c:var:: int TS_MIME_LEN_SUBJECTTS_MIME_LEN_SUBJECT

.. c:var:: const char* TS_MIME_FIELDSUMMARY
  "Summary"

.. c:var:: int TS_MIME_LEN_SUMMARY

.. c:var:: const char* TS_MIME_FIELDTE
  "TE"

.. c:var:: int TS_MIME_LEN_TE

.. c:var:: const char* TS_MIME_FIELDTRANSFER_ENCODING
  "Transfer-Encoding"

.. c:var:: int TS_MIME_LEN_TRANSFER_ENCODING

.. c:var:: const char* TS_MIME_FIELDUPGRADE
  "Upgrade"

.. c:var:: int TS_MIME_LEN_UPGRADE

.. c:var:: const char* TS_MIME_FIELDUSER_AGENT
  "User-Agent"

.. c:var:: int TS_MIME_LEN_USER_AGENT

.. c:var:: const char* TS_MIME_FIELDVARY
  "Vary"

.. c:var:: int TS_MIME_LEN_VARY

.. c:var:: const char* TS_MIME_FIELDVIA
  "Via"

.. c:var:: int TS_MIME_LEN_VIA

.. c:var:: const char* TS_MIME_FIELDWARNING
  "Warning"

.. c:var:: int TS_MIME_LEN_WARNING

.. c:var:: const char* TS_MIME_FIELDWWW_AUTHENTICATE
  "Www-Authenticate"

.. c:var:: int TS_MIME_LEN_WWW_AUTHENTICATE

.. c:var:: const char* TS_MIME_FIELDXREF
  "Xref"

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
