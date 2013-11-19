.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. default-domain:: c

============================
TSMimeHdrFieldValueStringGet
============================


Synopsis
========

`#include <ts/ts.h>`

.. function::  const char* TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int* value_len_ptr)
.. function::  int TSMimeHdrFieldValueIntGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
.. function::  int64_t TSMimeHdrFieldValueInt64Get(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
.. function::  unsigned int TSMimeHdrFieldValueUintGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
.. function::  time_t TSMimeHdrFieldValueDateGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)


Description
===========

MIME headers and fields can be components of request headers, response
headers, or standalone headers created within your plugin. The various
functions here are all used to access header values of specific types, but it
is up to the caller to know if a header has appropriate semantics for the API
used. For all but :func:`TSMimeHdrFieldValueStringGet`, an appropriate data
conversion algorithm is applied to the header field string.

All APIs takes an argument with the :type:`TSMBuffer` marshal buffer, and the
:type:`TSMLoc` for the location to headers. In addition, the required
:data:`field` argument is the locator of a specific header value, as returned
by e.g. :func:`TSMimeHdrFieldFind`.

Within the header field, comma separated values can be retrieved with an index
(:data:`idx`) ranging from 0 to the max number of fields for this value; this
max is retrieved using :func:`TSMimeHdrFieldValuesCount`. An :data:`idx` value of
``-1`` has the semantics of retrieving the entire header value, regardless of
how many comma separated values there are. If a header is not comma separated,
an :data:`idx` of :data:`0` or :data:`-1` are the same, but the latter is
definitely preferred and recommended.

For :func:`TSMimeHdrFieldValueStringGet`, an additional parameter can be
provided, which is populated with the length of the string returned. If this
argument is NULL, it is not derefenced and no length is returned.


Return values
=============

All functions returns the header value with a type matching the respective
function name. Using :func:`TSMimeHdrFieldValueDateGet` on a header which
does not have date-time semantics always returns :data:`0`.



Examples
========

This examples show how to retrieve and copy a specific header.

::

    #include <string.h>
    #include <ts/ts.h>

    int
    get_content_type(TSHttpTxn txnp, char* buf, size_t buf_size)
    {
      TSMBuffer bufp;
      TSMLoc hdrs;
      TSMLoc ctype_field;
      int len = -1;

      if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdrs)) {
        ctype_field = TSMimeHdrFieldFind(bufp, hdrs, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);

        if (TS_NULL_MLOC != ctype_field) {
          const char* str = TSMimeHdrFieldValueStringGet(bufp, hdrs, ctype_field, -1, &len);

          if (len > buf_size)
            len = buf_size;
          memcpy(buf, str, len);
          TSHandleMLocRelease(bufp, hdrs, ctype_field);
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrs);
      }

      return len;
    }


See also
========

:manpage:`TSAPI(3ts)`, :manpage:`TSMBufferCreate(3ts)`, :manpage:`TSMimeHdrFieldValuesCount`
