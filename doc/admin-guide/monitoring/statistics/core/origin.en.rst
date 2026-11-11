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

.. _admin-stats-core-origin:

Origin Server
*************

.. ts:stat:: global proxy.process.http.origin_server_total_request_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_total_response_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.origin_server_total_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_request_document_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_request_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_response_document_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_response_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.pool_lock_contention integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.migration_failure integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_no_keep_alive integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_eos integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_plugin_tunnel integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_transform_read integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_no_sharing integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_no_keep_alive integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_invalid_response integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_invalid_request integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_modified integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_misc integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.cleanup_entry integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_abort integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin.reuse_fail integer
   :type counter
   :units decisions

   Counts session-pool probes that cannot acquire a lock or allocate an
   origin transaction. A failed thread-local probe is counted even if the
   subsequent global-pool probe succeeds or finds no matching session. If
   both probes fail, one acquisition attempt increments this counter twice.
   This is not a count of distinct transactions or failed requests, and it
   can increase alongside ``proxy.process.http.origin.reuse`` or
   ``proxy.process.http.origin.not_found`` for the same acquisition attempt.

.. ts:stat:: global proxy.process.http.origin.retry_admitted integer
   :type counter
   :units decisions

   Counts admission decisions after an origin reports that a request was
   not processed and |TS| confirms that its body, if present, is replayable.
   A transaction can contribute more than once if successive attempts are
   rejected by the origin. Other retry limits still apply, so admission does
   not imply that a retry was issued or completed. This is not a count of
   distinct transactions.

.. ts:stat:: global proxy.process.http.origin.retry_body_unavailable integer
   :type counter
   :units decisions

   Counts decisions rejecting a retry that an origin reports as unprocessed
   because a complete, replayable request body is unavailable. This counts
   decisions rather than distinct transactions. Once this condition is detected,
   |TS| treats the request as non-retryable through both direct-origin and parent
   retry paths. Parent health handling therefore follows the existing
   non-retryable-request branches; this counter is not a parent health metric.
