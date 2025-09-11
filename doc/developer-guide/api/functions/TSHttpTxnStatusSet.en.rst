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

TSHttpTxnStatusSet
******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpTxnStatusSet(TSHttpTxn txnp, TSHttpStatus status)

.. function:: void TSHttpTxnStatusSet(TSHttpTxn txnp, TSHttpStatus status, std::string_view setter)

Description
===========

:func:`TSHttpTxnStatusSet` sets the transaction's internal status state, which triggers
Traffic Server's error handling system. This is typically used for access control,
authentication failures, and early transaction processing. Traffic Server will
automatically generate an appropriate error response body.

:arg:`txnp` is the associated transaction for the new status.

:arg:`status` is the HTTP status code to set.

:arg:`setter` (overload) is an optional identifying label for the entity setting the status
(e.g., plugin name), used for logging purposes. The setter information can be retrieved
using the 'prscs' log field. If empty, does not change the current setter value.
Defaults to empty string.

This function is commonly used by plugins that need to terminate a transaction early
with an error status. Unlike :func:`TSHttpHdrStatusSet`, this function affects the
transaction state rather than just the HTTP headers.

The ``setter`` parameter provides a convenient way to track which component set the status
for debugging and logging purposes.

Return Values
=============

:func:`TSHttpTxnStatusSet` returns no value.

See Also
========

:manpage:`TSHttpHdrStatusSet(3ts)`
