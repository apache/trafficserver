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

.. _admin-stats-core-ssl:

SSL/TLS
*******

.. ts:stat:: global proxy.process.ssl.origin_server_bad_cert integer
   :type: counter

   Indicates the number of certificates presented by origin servers which
   contained invalid information, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_cert_verify_failed integer
   :type: counter

   The number of origin server SSL certificates presented which failed
   verification, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_decryption_failed integer
   :type: counter

   The number of SSL connections to origin servers which returned data that
   could not be properly decrypted, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_expired_cert integer
   :type: counter

   The number of SSL connections to origin servers for which expired origin
   certificates were presented, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_other_errors integer
   :type: counter

   The number of SSL connections to origin servers which encountered otherwise
   uncategorized errors, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_revoked_cert integer
   :type: counter

   The number of SSL connections to origin servers during which a revoked
   certificate was presented by the origin, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_unknown_ca integer
   :type: counter

   The number of SSL connections to origin servers during which the origin
   presented a certificate signed by an unrecognized Certificate Authority,
   since statistics collection began.

.. ts:stat:: global proxy.process.ssl.origin_server_unknown_cert integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.origin_server_wrong_version integer
   :type: counter

   The number of SSL connections to origin servers which were terminated due to
   unsupported SSL/TLS protocol versions, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.ssl_error_ssl integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_error_syscall integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_session_cache_eviction integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_session_cache_hit integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_session_cache_lock_contention integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_session_cache_miss integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_session_cache_new_session integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.ssl_sni_name_set_failure integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_handshake_time integer
   :type: counter
   :units: milliseconds

   The total amount of time spent performing SSL/TLS handshakes for new sessions
   since statistics collection began.

.. ts:stat:: global proxy.process.ssl.total_attempts_handshake_count_in integer
   :type: counter

   The total number of inbound SSL/TLS handshake attempts received since
   statistics collection began.

.. ts:stat:: global proxy.process.ssl.total_success_handshake_count_in integer
   :type: counter

   The total number of inbound SSL/TLS handshakes successfully performed since
   statistics collection began.

.. ts:stat:: global proxy.process.ssl.total_attempts_handshake_count_out integer
   :type: counter

   The total number of outbound SSL/TLS handshake attempts made since
   statistics collection began.

.. ts:stat:: global proxy.process.ssl.total_success_handshake_count_out integer
   :type: counter

   The total number of outbound SSL/TLS handshakes successfully performed since
   statistics collection began.

.. ts:stat:: global proxy.process.ssl.total_ticket_keys_renewed integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_tickets_created integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_tickets_not_found integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_tickets_renewed integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_tickets_verified integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.total_tickets_verified_old_key integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.user_agent_bad_cert integer
   :type: counter

   Incoming client SSL connections which have presented invalid data in lieu of
   a client certificate, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_cert_verify_failed integer
   :type: counter

   Incoming client SSL connections which presented a client certificate that did
   not pass verification, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_decryption_failed integer
   :type: counter

   Incoming client SSL connections which failed to be properly decrypted, since
   statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_expired_cert integer
   :type: counter

   Incoming client SSL connections which presented a client certificate that had
   already expired, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_other_errors integer
   :type: counter

   Incoming client SSL connections which experienced otherwise uncategorized
   errors, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_revoked_cert integer
   :type: counter

   Incoming client SSL connections which presented a client certificate that had
   been revoked, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_session_hit integer
   :type: counter

   Incoming client SSL connections which successfully used a previously
   negotiated session, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_session_miss integer
   :type: counter

   Incoming client SSL connections which unsuccessfully attempted to use a
   previously negotiated session, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_sessions integer
   :type: counter

   A counter indicating the number of SSL sessions negotiated for incoming
   client connections, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_session_timeout integer
   :type: counter

   Incoming client SSL connections which terminated with an expired session,
   since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_unknown_ca integer
   :type: counter

   Incoming client SSL connections which presented a client certificate signed
   by an unrecognized Certificate Authority, since statistics collection began.

.. ts:stat:: global proxy.process.ssl.user_agent_unknown_cert integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.user_agent_wrong_version integer
   :type: counter

   Incoming client SSL connections terminated due to an unsupported or disabled
   version of SSL/TLS, since statistics collection began.
