.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../../../common.defs

.. _admin-stats-core-log:

Logging
*******

.. ts:stat:: global proxy.node.log.bytes_flush_to_disk integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_lost_before_flush_to_disk integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_lost_before_preproc integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_lost_before_sent_to_network integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_lost_before_written_to_disk integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_received_from_network_avg_10s integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_received_from_network integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_sent_to_network_avg_10s integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_sent_to_network integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.bytes_written_to_disk integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_access_aggr integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_access_fail integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_access_full integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_access_ok integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_access_skip integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_error_aggr integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_error_fail integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_error_full integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_error_ok integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.event_log_error_skip integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.num_flush_to_disk integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.num_lost_before_flush_to_disk integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.num_lost_before_sent_to_network integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.num_received_from_network integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.node.log.num_sent_to_network integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.log.bytes_flush_to_disk integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_lost_before_flush_to_disk integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_lost_before_preproc integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_lost_before_sent_to_network integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_lost_before_written_to_disk integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_received_from_network integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_sent_to_network integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.bytes_written_to_disk integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.log.event_log_access_aggr integer
   :type: counter

.. ts:stat:: global proxy.process.log.event_log_access_fail integer
   :type: counter

   Indicates the number of times |TS| has encountered a failure while attempting
   to log an event to the access logs facility.

.. ts:stat:: global proxy.process.log.event_log_access_full integer
   :type: counter

   Indicates the number of times |TS| has been unable to log an event to the
   access logs facility due to insufficient space.

.. ts:stat:: global proxy.process.log.event_log_access_ok integer
   :type: counter

   Indicates the number of times |TS| has successfully logged an event to the
   access logs facility.

.. ts:stat:: global proxy.process.log.event_log_access_skip integer
   :type: counter

   Indicates the number of times |TS| has skipped logging an event to the access
   logs facility.

.. ts:stat:: global proxy.process.log.event_log_error_aggr integer
   :type: counter

.. ts:stat:: global proxy.process.log.event_log_error_fail integer
   :type: counter

   Indicates the number of times |TS| has encountered a failure while attempting
   to log an event to the error logs facility.

.. ts:stat:: global proxy.process.log.event_log_error_full integer
   :type: counter

   Indicates the number of times |TS| has been unable to log an event to the
   error logs facility due to insufficient space.

.. ts:stat:: global proxy.process.log.event_log_error_ok integer
   :type: counter

   Indicates the number of times |TS| has successfully logged an event to the
   error logs facility.

.. ts:stat:: global proxy.process.log.event_log_error_skip integer
   :type: counter

   Indicates the number of times |TS| has skipped logging an event to the error
   logs facility.

.. ts:stat:: global proxy.process.log.log_files_open integer
   :type: gauge

   Indicates the number of log files currently open by |TS|.

.. ts:stat:: global proxy.process.log.log_files_space_used integer
   :type: gauge
   :unit: bytes

   Indicates the number of bytes currently in use by |TS| log files.

.. ts:stat:: global proxy.process.log.num_flush_to_disk integer
   :type: counter

   The number of events |TS| has flushed to log files on disk, since statistics
   collection began.

.. ts:stat:: global proxy.process.log.num_lost_before_flush_to_disk integer
   :type: counter

.. ts:stat:: global proxy.process.log.num_lost_before_sent_to_network integer
   :type: counter

.. ts:stat:: global proxy.process.log.num_received_from_network integer
   :type: counter

.. ts:stat:: global proxy.process.log.num_sent_to_network integer
   :type: counter


