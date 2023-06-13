/** @file

  Asynchronous networking API

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

#include "AcceptOptions.h"
#include "I_Net.h"

AcceptOptions &
AcceptOptions::reset()
{
  local_port = 0;
  local_ip.invalidate();
  accept_threads        = -1;
  ip_family             = AF_INET;
  etype                 = ET_NET;
  localhost_only        = false;
  frequent_accept       = true;
  recv_bufsize          = 0;
  send_bufsize          = 0;
  sockopt_flags         = 0;
  packet_mark           = 0;
  packet_tos            = 0;
  packet_notsent_lowat  = 0;
  tfo_queue_length      = 0;
  f_inbound_transparent = false;
  f_mptcp               = false;
  f_proxy_protocol      = false;
  return *this;
}
