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

TSRemapNextHopStrategyGet
*************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void const* TSRemapNextHopStrategyGet()

Description
===========

Gets a pointer to the current remap rule being loaded.
This may be nullptr indicating that parent.config is in use.

This function may ONLY be called during TSRemapNewInstance.
The resulting strategy pointer is valid for all subsequent transactions.

.. note::

   This strategy pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins will become invalid when ATS
   configs are reloaded and should be reset with :func:`TSRemapNewInstance`

See Also
========

:func:`TSRemapNextHopStrategySet`
