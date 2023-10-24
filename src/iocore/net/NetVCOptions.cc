/**@file

   A brief file description

 @section license License

   Licensed to the Apache Software
   Foundation(ASF) under one
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

#include "iocore/net/NetVCOptions.h"
#include "iocore/net/Net.h"
#include "iocore/net/Socks.h"

void
NetVCOptions::reset()
{
  ip_proto  = USE_TCP;
  ip_family = AF_INET;
  local_ip.invalidate();
  local_port    = 0;
  addr_binding  = ANY_ADDR;
  socks_support = NORMAL_SOCKS;
  socks_version = SOCKS_DEFAULT_VERSION;
  socket_recv_bufsize =
#if defined(RECV_BUF_SIZE)
    RECV_BUF_SIZE;
#else
    0;
#endif
  socket_send_bufsize  = 0;
  sockopt_flags        = 0;
  packet_mark          = 0;
  packet_tos           = 0;
  packet_notsent_lowat = 0;

  etype = ET_NET;

  sni_servername              = nullptr;
  ssl_servername              = nullptr;
  sni_hostname                = nullptr;
  ssl_client_cert_name        = nullptr;
  ssl_client_private_key_name = nullptr;
}

void
NetVCOptions::set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark,
                             unsigned long _packet_tos, unsigned long _packet_notsent_lowat)
{
  socket_recv_bufsize  = _recv_bufsize;
  socket_send_bufsize  = _send_bufsize;
  sockopt_flags        = _opt_flags;
  packet_mark          = _packet_mark;
  packet_tos           = _packet_tos;
  packet_notsent_lowat = _packet_notsent_lowat;
}
