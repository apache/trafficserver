/** @file

  This file implements an I/O Processor for network I/O

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
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

 */

#pragma once

#include "tscore/ink_inet.h"

struct AcceptOptions {
  using self = AcceptOptions; ///< Self reference type.

  /// Port on which to listen.
  /// 0 => don't care, which is useful if the socket is already bound.
  int local_port = 0;
  /// Local address to bind for accept.
  /// If not set -> any address.
  IpAddr local_ip;
  /// IP address family.
  /// @note Ignored if an explicit incoming address is set in the
  /// the configuration (@c local_ip). If neither is set IPv4 is used.
  int ip_family = AF_INET;
  /// Should we use accept threads? If so, how many?
  int accept_threads = -1;
  /** If @c true, the continuation is called back with
      @c NET_EVENT_ACCEPT_SUCCEED
      or @c NET_EVENT_ACCEPT_FAILED on success and failure resp.
  */
  bool localhost_only = false;
  /// Are frequent accepts expected?
  /// Default: @c false.
  bool frequent_accept = true;

  /// Socket receive buffer size.
  /// 0 => OS default.
  int recv_bufsize = 0;
  /// Socket transmit buffer size.
  /// 0 => OS default.
  int send_bufsize = 0;
  /// defer accept for @c sockopt.
  /// 0 => OS default.
  int defer_accept = 0;
  /// Socket options for @c sockopt.
  /// 0 => do not set options.
  uint32_t sockopt_flags        = 0;
  uint32_t packet_mark          = 0;
  uint32_t packet_tos           = 0;
  uint32_t packet_notsent_lowat = 0;

  int tfo_queue_length = 0;

  /** Transparency on client (user agent) connection.
      @internal This is irrelevant at a socket level (since inbound
      transparency must be set up when the listen socket is created)
      but it's critical that the connection handling logic knows
      whether the inbound (client / user agent) connection is
      transparent.
  */
  bool f_inbound_transparent = false;

  /** MPTCP enabled on listener.
      @internal For logging and metrics purposes to know whether the
      listener enabled MPTCP or not.
  */
  bool f_mptcp = false;

  /// Proxy Protocol enabled
  bool f_proxy_protocol = false;
};
