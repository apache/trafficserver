.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

Plugin Statistics
**********************

A plugin can create statistics (metrics) that are accessible in the same way as |TS| core
statistics. In general monitoring the behavior of plugins in production is easier to do in this way
in contrast to processing log files.

Synopsis
========

`#include <ts/ts.h>`

.. function:: int TSStatCreate(const char * name, TSRecordDataType type, TSStatPersistence persistence, TSStatSync sync_style)
.. function:: TSReturnCode TSStatFindName(const char * name, int * idx_ptr)

.. function:: TSMgmtInt TSStatIntGet(int idx)
.. function:: void TSStatIntSet(int idx, TSMgmtInt value)
.. function:: void TSStatIntIncrement(int idx, TSMgmtInt value)
.. function:: void TSStatIntDecrement(int idx, TSMgmtInt value)

Description
===========

A plugin statistic is created by :func:`TSStatCreate`. The :arg:`name` must be globally unique and
should follow the standard dotted tag form. To avoid collisions and for easy of use the first tag
should be the plugin name or something easily derived from it. Currently only integers are suppored
therefore :arg:`type` must be :macro:`TS_RECORD_DATATYPE_INT`. The return value is the index of the
statistic. In general thsi should work but if it doesn't it will :code:`assert`. In particular,
creating the same statistic twice will fail in this way, which can happen if statistics are created
as part of or based on configuration files and |TS| is reloaded.

:func:`TSStatFindName` locates a statistic by :arg:`name`. If found the function returns
:const:`TS_SUCCESS` and the value pointed at by :arg:`idx_ptr` is updated to be the index of the
statistic. Otherwise it returns ``TS_ERROR``.

The values in statistics are manipulated by :func:`TSStatIntSet` to set the statistic directly,
:func:`TSStatIntIncrement` to increase it by :arg:`value`, and :func:`TSStatIntDecrement` to
decrease it by :arg:`value`.

Return Values
=============

:func:`TSMgmtStringCreate` and :func:`TSMgmtIntCreate` return :const:`TS_SUCCESS` if the management
value was created and :const:`TS_ERROR` if not.


See Also
========

:ref:`developer-plugins-add-statistics`
:manpage:`TSAPI(3ts)`
