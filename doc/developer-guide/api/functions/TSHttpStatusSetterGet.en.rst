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

TSHttpStatusSetterGet
*********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: std::string_view TSHttpStatusSetterGet(TSHttpTxn txnp)

Description
===========

:func:`TSHttpStatusSetterGet` retrieves the identifying label for the entity that last
set the HTTP status for the transaction. This provides a way to programmatically
determine which component (plugin, core, etc.) was responsible for setting the current
HTTP status code.

:arg:`txnp` is the transaction handle for which the setter is being retrieved.

The setter information can be retrieved using the 'plss' log field.

The setter information is set by calling :func:`TSHttpStatusSetterSet` or by using
:func:`TSHttpTxnStatusSet` with the ``setter`` parameter.

Return Values
=============

:func:`TSHttpStatusSetterGet` returns the setter label, or an empty string if no setter
has been recorded. The returned string_view is valid for the transaction lifetime.

See Also
========

:manpage:`TSHttpStatusSetterSet(3ts)`,
:manpage:`TSHttpTxnStatusSet(3ts)`,
:manpage:`TSHttpHdrStatusSet(3ts)`
