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

TSMgmtSourceGet
***************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSMgmtSourceGet(const char * var_name, TSMgmtSource * result)

Description
===========

Get the source of a value for a configuration variable. :arg:`var_name` is the name of the variable
as a nul terminated string. The source value is stored in :arg:`result`. The function can return
failure if :arg:`var_name` is not found.

Types
=====

.. type:: TSMgmtSource

   Source of the current value for a management (configuration) value.

   .. macro:: TS_MGMT_SOURCE_NULL

      Invalid value, no source available. This is primarily used as an initialization or error value
      and should be returned only when the API call fails.

   .. macro:: TS_MGMT_SOURCE_DEFAULT

      The default value provided by the |TS| core.

   .. macro:: TS_MGMT_SOURCE_PLUGIN

      The configuration variable was created by a plugin and the value is the default value provided
      by a plugin.

   .. macro:: TS_MGMT_SOURCE_EXPLICIT

      The value has been set in :file:`records.config`. Note this value is returned even if the
      variable was explicitly set to the default value.

   .. macro:: TS_MGMT_SOURCE_ENV

      The value was retrieved from the process environment, overriding the default value.

Return Values
=============

:data:`TS_SUCCESS` if the :arg:`var_name` was found, :data:`TS_ERROR` if not.
