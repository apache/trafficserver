.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSMimeHdrFieldAppend
********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)

Description
===========

Returns the :c:type:`TSMLoc` location of a specified MIME field from
within the MIME header located at :arg:`hdr`.

The retrieved_str parameter specifies which field to retrieve.  For
each MIME field in the MIME header, a pointer comparison is done
between the field name and retrieved_str.  This is a much quicker
retrieval function than :c:func:`TSMimeHdrFieldFind` since it obviates
the need for a string comparison.  However, retrieved_str must be one
of the predefined field names of the form :c:data:`TS_MIME_FIELD_XXX`
for the call to succeed.  Release the returned :c:type:`TSMLoc` handle
with a call to :c:func:`TSHandleMLocRelease`.

.. XXX The above is surely from the documentation of another function. Confirm
       and remove from here (or relocate to the appropriate function's doc).
