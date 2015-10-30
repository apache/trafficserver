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

TSMimeHdrParse
**************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSParseResult TSMimeHdrParse(TSMimeParser parser, TSMBuffer bufp, TSMLoc offset, const char ** start, const char * end)

Description
===========

Parses a MIME header.

The MIME header must have already been allocated and both :arg:`bufp` and
:arg:`hdr_loc` must point within that header.  It is possible to parse a MIME
header a single byte at a time using repeated calls to
:c:func:`TSMimeHdrParse`.  As long as an error does not occur,
:c:func:`TSMimeHdrParse` consumes each single byte and asks for more.
