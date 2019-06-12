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

.. default-domain:: c

TSUrlStringGet
**************

Traffic Server URL string representations API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: char * TSUrlStringGet(TSMBuffer bufp, TSMLoc offset, int * length)
.. function:: char * TSHttpTxnEffectiveUrlStringGet(TSHttpTxn txn, int * length)
.. function:: int TSUrlLengthGet(TSMBuffer bufp, TSMLoc offset)
.. function:: void TSUrlPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp)

Description
===========

The URL data structure is a parsed version of a standard internet URL. The
Traffic Server URL API provides access to URL data stored in marshal
buffers. The URL functions can create, copy, retrieve or delete entire URLs,
and retrieve or modify parts of URLs, such as their host, port or scheme
information.

:func:`TSUrlStringGet` constructs a string representation of the URL located
at :arg:`offset` within the marshal buffer :arg:`bufp`.  (However :arg:`bufp` is actually superfluous and may be null.)
:func:`TSUrlStringGet` stores the length of the allocated string in the
parameter :arg:`length`. This is the same length that :func:`TSUrlLengthGet`
returns. The returned string is allocated by a call to :func:`TSmalloc` and
must be freed by a call to :func:`TSfree`. If length is :literal:`NULL` then no
attempt is made to de-reference it.

:func:`TSHttpTxnEffectiveUrlStringGet` is similar to :func:`TSUrlStringGet`. The two differences are

*  The source is transaction :arg:`txn` in order to have access to the full request.
*  It combines, if needed, both the explicit url and the ``Host`` field. This is
   done if the explicit URL does not have a host and the ``Host`` field does.

This function is useful to guarantee a URL that is as complete as possible given
the specific request.

:func:`TSUrlLengthGet` calculates the length of the URL located at
:arg:`offset` within the marshal buffer bufp as if it were returned as a
string. This length will be the same as the length returned by
:func:`TSUrlStringGet`.

:func:`TSUrlPrint` formats a URL stored in an :type:`TSMBuffer` to an
:type:`TSIOBuffer`.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSmalloc(3ts)`,
:manpage:`TSUrlCreate(3ts)`,
:manpage:`TSUrlHostGet(3ts)`,
:manpage:`TSUrlHostSet(3ts)`,
:manpage:`TSUrlPercentEncode(3ts)`
