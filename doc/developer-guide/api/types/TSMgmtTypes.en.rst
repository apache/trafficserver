
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

.. cpp:type:: OpType

.. c:macro:: RECORD_SET

.. c:macro:: RECORD_GET

.. c:macro:: PROXY_STATE_GET

.. c:macro:: PROXY_STATE_SET

.. c:macro:: RECONFIGURE

.. c:macro:: RESTART

.. c:macro:: BOUNCE

.. c:macro:: EVENT_RESOLVE

.. c:macro:: EVENT_GET_MLT

.. c:macro:: EVENT_ACTIVE

.. c:macro:: EVENT_REG_CALLBACK,

.. c:macro:: EVENT_UNREG_CALLBACK

.. c:macro:: EVENT_NOTIFY

.. c:macro:: STATS_RESET_NODE

.. c:macro:: STORAGE_DEVICE_CMD_OFFLINE

.. c:macro:: RECORD_MATCH_GET

.. c:macro:: API_PING

.. c:macro:: SERVER_BACKTRACE

.. c:macro:: RECORD_DESCRIBE_CONFIG

.. c:macro:: LIFECYCLE_MESSAGE

.. c:macro:: UNDEFINED_OP


TSMgmtError
===========
.. cpp:type:: TSMgmtError

.. c:macro:: TS_ERR_OKAY

.. c:macro:: TS_ERR_READ_FILE

.. c:macro:: TS_ERR_WRITE_FILE

.. c:macro:: TS_ERR_PARSE_CONFIG_RULE

.. c:macro:: TS_ERR_INVALID_CONFIG_RULE

.. c:macro:: TS_ERR_NET_ESTABLISH

.. c:macro:: TS_ERR_NET_READ

.. c:macro:: TS_ERR_NET_WRITE

.. c:macro:: TS_ERR_NET_EOF

.. c:macro:: TS_ERR_NET_TIMEOUT

.. c:macro:: TS_ERR_SYS_CALL

.. c:macro:: TS_ERR_PARAMS

.. c:macro:: TS_ERR_NOT_SUPPORTED

.. c:macro:: TS_ERR_PERMISSION_DENIED

.. c:macro:: TS_ERR_FAIL


MgmtMarshallType
================
.. cpp:type:: MgmtMarshallType

.. c:macro:: MGMT_MARSHALL_INT

.. c:macro:: MGMT_MARSHALL_LONG

.. c:macro:: MGMT_MARSHALL_STRING

.. c:macro:: MGMT_MARSHALL_DATA
