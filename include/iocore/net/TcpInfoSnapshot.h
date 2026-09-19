/** @file

  A snapshot of the TCP_INFO fields that ATS reports.

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

#include <cstdint>

/** The subset of @c TCP_INFO that ATS reports.
 *
 * Sampling a connection copies these out of the kernel, so the values stay
 * available after the connection itself is gone. The kernel smooths both times
 * over the life of the connection, so they describe the path rather than any
 * single segment.
 *
 * This lives in its own header so that consumers which only report the values,
 * such as logging, do not have to include the network stack.
 */
struct TcpInfoSnapshot {
  int64_t rtt      = 0; ///< Smoothed round trip time, microseconds.
  int64_t rttvar   = 0; ///< Round trip time variance, microseconds.
  int64_t retrans  = 0; ///< Segments retransmitted since connection open, up to sampling time.
  int64_t snd_cwnd = 0; ///< Send congestion window: segments on Linux, bytes on FreeBSD.
};
