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

TSHttpStatusSetterSet
*********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpStatusSetterSet(TSHttpTxn txnp, std::string_view setter)

Description
===========

:func:`TSHttpStatusSetterSet` sets an identifying label for the entity that last set
the HTTP status for the transaction. This function allows tracking which plugin
was responsible for setting the current HTTP status code.

:arg:`txnp` is the transaction for which the status was set.

:arg:`setter` is an identifying label for the entity setting the status (e.g., plugin name).
If empty, clears the current setter information.

The setter information is used for logging purposes and can be retrieved using the 'plss'
log field or the :func:`TSHttpStatusSetterGet` function. This is particularly useful for
debugging when multiple plugins or components might be setting status codes and you need
to identify which one was responsible for the final status.

Note that :func:`TSHttpTxnStatusSet` and :func:`TSHttpHdrStatusSet` can optionally take
parameters to set the setter information along with the new status, making
this function unnecessary in many cases. However, :func:`TSHttpHdrStatusSet` can be used
when the attending txn is not easily available, making this function necessary in those cases.

Return Values
=============

:func:`TSHttpStatusSetterSet` returns no value.

Examples
========

.. code-block:: cpp

    // Set status and track that this plugin set it
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_FORBIDDEN);
    TSHttpStatusSetterSet(txnp, "access_control_plugin");

    // Or use the convenience form:
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_FORBIDDEN, "access_control_plugin");

    // Clear setter information
    TSHttpStatusSetterSet(txnp, "");

See Also
========

:manpage:`TSHttpTxnStatusSet(3ts)`,
:manpage:`TSHttpStatusSetterGet(3ts)`,
:manpage:`TSHttpHdrStatusSet(3ts)`
