
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

TSMgmt
******

Synopsis
========

Macros used for RPC communications.

Management Signals
==================

.. c:macro:: MGMT_SIGNAL_PID

.. c:macro:: MGMT_SIGNAL_CONFIG_ERROR

.. c:macro:: MGMT_SIGNAL_SYSTEM_ERROR

.. c:macro:: MGMT_SIGNAL_CONFIG_FILE_READ

.. c:macro:: MGMT_SIGNAL_CACHE_ERROR

.. c:macro:: MGMT_SIGNAL_CACHE_WARNING

.. c:macro:: MGMT_SIGNAL_LOGGING_ERROR

.. c:macro:: MGMT_SIGNAL_LOGGING_WARNING

.. c:macro:: MGMT_SIGNAL_PLUGIN_SET_CONFIG

.. c:macro:: MGMT_SIGNAL_LIBRECORDS

.. c:macro:: MGMT_SIGNAL_HTTP_CONGESTED_SERVER

.. c:macro:: MGMT_SIGNAL_HTTP_ALLEVIATED_SERVER

.. c:macro:: MGMT_SIGNAL_CONFIG_FILE_CHILD


Management Events
==================

.. c:macro:: MGMT_EVENT_SYNC_KEY

.. c:macro:: MGMT_EVENT_SHUTDOWN

.. c:macro:: MGMT_EVENT_RESTART

.. c:macro:: MGMT_EVENT_BOUNCE

.. c:macro:: MGMT_EVENT_CLEAR_STATS

.. c:macro:: MGMT_EVENT_CONFIG_FILE_UPDATE

.. c:macro:: MGMT_EVENT_PLUGIN_CONFIG_UPDATE

.. c:macro:: MGMT_EVENT_ROLL_LOG_FILES

.. c:macro:: MGMT_EVENT_LIBRECORDS

.. c:macro:: MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE

.. c:macro:: MGMT_EVENT_LIFECYCLE_MESSAGE


OpTypes
=======

Possible operations or messages that can be sent between TM and remote clients.

.. cpp:enum:: OpType

   .. cpp:enumerator:: RECORD_SET

   .. cpp:enumerator:: RECORD_GET

   .. cpp:enumerator:: PROXY_STATE_GET

   .. cpp:enumerator:: PROXY_STATE_SET

   .. cpp:enumerator:: RECONFIGURE

   .. cpp:enumerator:: RESTART

   .. cpp:enumerator:: BOUNCE

   .. cpp:enumerator:: EVENT_RESOLVE

   .. cpp:enumerator:: EVENT_GET_MLT

   .. cpp:enumerator:: EVENT_ACTIVE

   .. cpp:enumerator:: EVENT_REG_CALLBACK

   .. cpp:enumerator:: EVENT_UNREG_CALLBACK

   .. cpp:enumerator:: EVENT_NOTIFY

   .. cpp:enumerator:: STATS_RESET_NODE

   .. cpp:enumerator:: STORAGE_DEVICE_CMD_OFFLINE

   .. cpp:enumerator:: RECORD_MATCH_GET

   .. cpp:enumerator:: API_PING

   .. cpp:enumerator:: SERVER_BACKTRACE

   .. cpp:enumerator:: RECORD_DESCRIBE_CONFIG

   .. cpp:enumerator:: LIFECYCLE_MESSAGE

   .. cpp:enumerator:: UNDEFINED_OP


TSMgmtError
===========
.. cpp:enum:: TSMgmtError

   .. cpp:enumerator:: TS_ERR_OKAY

   .. cpp:enumerator:: TS_ERR_READ_FILE

   .. cpp:enumerator:: TS_ERR_WRITE_FILE

   .. cpp:enumerator:: TS_ERR_PARSE_CONFIG_RULE

   .. cpp:enumerator:: TS_ERR_INVALID_CONFIG_RULE

   .. cpp:enumerator:: TS_ERR_NET_ESTABLISH

   .. cpp:enumerator:: TS_ERR_NET_READ

   .. cpp:enumerator:: TS_ERR_NET_WRITE

   .. cpp:enumerator:: TS_ERR_NET_EOF

   .. cpp:enumerator:: TS_ERR_NET_TIMEOUT

   .. cpp:enumerator:: TS_ERR_SYS_CALL

   .. cpp:enumerator:: TS_ERR_PARAMS

   .. cpp:enumerator:: TS_ERR_NOT_SUPPORTED

   .. cpp:enumerator:: TS_ERR_PERMISSION_DENIED

   .. cpp:enumerator:: TS_ERR_FAIL

MgmtMarshallType
================
.. cpp:enum:: MgmtMarshallType

   .. cpp:enumerator:: MGMT_MARSHALL_INT

   .. cpp:enumerator:: MGMT_MARSHALL_LONG

   .. cpp:enumerator:: MGMT_MARSHALL_STRING

   .. cpp:enumerator:: MGMT_MARSHALL_DATA
