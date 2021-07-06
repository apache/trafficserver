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

TSMgmtDataTypeGet
*****************

Synopsis
========

.. code-block:: c

    #include <ts/ts.h>

.. function:: TSReturnCode TSMgmtDataTypeGet(const char * var_name, TSRecordDataType * result)

Description
===========

Get the type of a value for a configuration variable. :arg:`var_name` is the name of the variable
as a null terminated string. The type value is stored in :arg:`result`. The function can return
:c:data:`TS_ERROR` if :arg:`var_name` is not found.

Types
=====

Check :type:`TSRecordDataType` for a detailed description.



Return Values
=============

:data:`TS_SUCCESS` if the :arg:`var_name` was found, :data:`TS_ERROR` if not.
