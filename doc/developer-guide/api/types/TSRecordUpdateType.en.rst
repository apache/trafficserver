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

TSRecordUpdateType
******************

Synopsis
========

`#include <ts/apidefs.h>`

.. c:type:: TSRecordUpdateType

Enum typedef.

Enumeration Members
===================

.. c:member:: TSRecordUpdateType TS_RECORDUPDATE_NULL

   The value cannot be updated. This is used primarily as a default value, actual instances should not use this.

.. c:member:: TSRecordUpdateType TS_RECORDUPDATE_DYNAMIC

   The value can be updated at runtime, including by using :program:`traffic_ctl`.

.. c:member:: TSRecordUpdateType TS_RECORDUPDATE_RESTART_TS

   The value is updated if the :program:`traffic_server` process is restarted.

.. c:member:: TSRecordUpdateType TS_RECORDUPDATE_RESTART_TM

   The value is updated if the :program:`traffic_manager` process is restarted.

Description
===========

