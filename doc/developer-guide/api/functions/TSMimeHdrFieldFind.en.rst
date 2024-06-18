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

.. default-domain:: cpp

TSMimeHdrFieldFind
******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSMLoc TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr, const char * name, int length)
.. function:: const char *TSMimeHdrStringToWKS(const char *str, int length)


Description
===========

Retrieves the :cpp:type:`TSMLoc` location of a specified MIME field from
within the MIME header located at :arg:`hdr`.

The :arg:`name` and :arg:`length` parameters specify which field to retrieve.
For each MIME field in the MIME header, a case insensitive string
comparison is done between the field name and :arg:`name`. If
:func:`TSMimeHdrFieldFind` cannot find the requested field, it
returns :var:`TS_NULL_MLOC`.  Release the returned :cpp:type:`TSMLoc`
handle with a call to :func:`TSHandleMLocRelease`.

The :arg:`name` argument is best specified using the pre-defined Well-Known strings, such as e.g.
``TS_MIME_FIELD_CACHE_CONTROL`` and ``TS_MIME_LEN_CACHE_CONTROL``. These WK constants
can also be looked up using :func:`TSMimeHdrStringToWKS`. If a header does
not have a WKS, this function will return a :code:`nullptr`.
