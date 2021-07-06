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

TSRecordType
************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:enum:: TSRecordType

   Effectively the scope of the record.

   .. c:enumerator:: TS_RECORDTYPE_NULL

      Invalid value. This is used to indicate a failure or for initialization.

   .. c:enumerator:: TS_RECORDTYPE_CONFIG

      A configuration record.

   .. c:enumerator:: TS_RECORDTYPE_PROCESS

   .. c:enumerator:: TS_RECORDTYPE_NODE

   .. c:enumerator:: TS_RECORDTYPE_CLUSTER

      Deprecated - shared among a cluster.

   .. c:enumerator:: TS_RECORDTYPE_LOCAL

      Deprecated - not shared among a cluster.

   .. c:enumerator:: TS_RECORDTYPE_PLUGIN

      Created by a plugin.

   .. c:enumerator:: TS_RECORDTYPE_ALL

Description
===========

The management role for a management value. In practice only :c:macro:`TS_RECORDTYPE_CONFIG` is usable.
