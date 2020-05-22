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

TSMimeHdrFieldGet
*****************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSMLoc TSMimeHdrFieldGet(TSMBuffer bufp, TSMLoc hdr, int idx)

Description
===========

Retrieves the location of a specified MIME field within the MIME
header located at :arg:`hdr` within :arg:`bufp`.

The :arg:`idx` parameter specifies which field to retrieve.  The fields are
numbered from :literal:`0` to ``TSMimeHdrFieldsCount(bufp, hdr_loc)`` - 1.  If
:arg:`idx` does not lie within that range then :c:type:`TSMimeHdrFieldGet`
returns :literal:`0`.  Release the returned handle with a call to
:c:type:`TSHandleMLocRelease`.
