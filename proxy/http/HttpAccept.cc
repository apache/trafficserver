/** @file

  A brief file description

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

#include "HttpAccept.h"
#include "IPAllow.h"
#include "HttpClientSession.h"
#include "I_Machine.h"


int
HttpAccept::mainEvent(int event, void *data)
{
  ink_release_assert(event == NET_EVENT_ACCEPT || event == EVENT_ERROR);
  ink_release_assert((event == NET_EVENT_ACCEPT) ? (data != 0) : (1));

  if (event == NET_EVENT_ACCEPT) {
    ////////////////////////////////////////////////////
    // if client address forbidden, close immediately //
    ////////////////////////////////////////////////////
    NetVConnection *netvc = (NetVConnection *) data;
    unsigned int client_ip = netvc->get_remote_ip();

    if (backdoor) {
      unsigned int lip = 0;
      unsigned char *plip = (unsigned char *) &lip;
      plip[0] = 127;
      plip[1] = 0;
      plip[2] = 0;
      plip[3] = 1;
      if (client_ip != this_machine()->ip && client_ip != lip
          && client_ip != HttpConfig::m_master.incoming_ip_to_bind_saddr) {
        char ip_string[32];
        unsigned char *p = (unsigned char *) &(client_ip);

        snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
        Warning("connect by disallowed client %s on backdoor, closing", ip_string);
        netvc->do_io_close();
        return (VC_EVENT_CONT);
      }
    } else {
      if (ip_allow_table && (!ip_allow_table->match(client_ip))) {
        char ip_string[32];
        unsigned char *p = (unsigned char *) &(client_ip);

        snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
        Warning("connect by disallowed client %s, closing", ip_string);
        netvc->do_io_close();
        return (VC_EVENT_CONT);
      }
    }

    netvc->attributes = attr;

    Debug("http_seq", "HttpAccept:mainEvent] accepted connection");
    HttpClientSession *new_session = THREAD_ALLOC_INIT(httpClientSessionAllocator, netvc->thread);

    new_session->new_connection(netvc, backdoor);
    return (EVENT_CONT);
  } else {
    /////////////////
    // EVENT_ERROR //
    /////////////////
    if (((long) data) == -ECONNABORTED) {
      /////////////////////////////////////////////////
      // Under Solaris, when accept() fails and sets //
      // errno to EPROTO, it means the client has    //
      // sent a TCP reset before the connection has  //
      // been accepted by the server...  Note that   //
      // in 2.5.1 with the Internet Server Supplement//
      // and also in 2.6 the errno for this case has //
      // changed from EPROTO to ECONNABORTED.        //
      /////////////////////////////////////////////////

      // FIX: add time to user_agent_hangup
      HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_pre_accept_hangups_stat, 0);
    }
    MachineFatal("HTTP accept received fatal error: errno = %d", -((int)(intptr_t)data));
    return (EVENT_CONT);
  }
}
