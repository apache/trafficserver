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

TSRemapNextHopStrategyFind
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSStrategy TSRemapNextHopStrategyFind(const char *name)

Description
===========

Gets a pointer to the specified :arg:`name` NextHopSelectionStrategy.
This may be nullptr, indicating that no strategy exists with the given
name or that the call was made outside of remap rule initialization
(e.g. from a globally loaded plugin).

This function may ONLY be called during TSRemapNewInstance. A call
made outside of remap rule initialization is logged to ``diags.log``
the first time it occurs.

.. note::

   :arg:`name` must not be nullptr; passing nullptr is a plugin API
   violation and triggers an assertion.

.. note::

   This returned pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins become invalid when ATS configs
   are reloaded and must be re-obtained during :func:`TSRemapNewInstance`

See Also
========

:func:`TSRemapNextHopStrategySet`
