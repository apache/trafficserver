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

/**************************************************************************

  P_DNSConnection.h
  Description:
  struct DNSConnection
  **************************************************************************/

#ifndef __P_DNSCONNECTION_H__
#define __P_DNSCONNECTION_H__

#include "I_EventSystem.h"

//
// Defines
//

#define NON_BLOCKING_CONNECT     true
#define BLOCKING_CONNECT         false
#define CONNECT_WITH_TCP         true
#define CONNECT_WITH_UDP         false
#define NON_BLOCKING             true
#define BLOCKING                 false
#define BIND_RANDOM_PORT         true
#define BIND_ANY_PORT            false
#define ENABLE_MC_LOOPBACK       true
#define DISABLE_MC_LOOPBACK      false
#define BC_NO_CONNECT      	 true
#define BC_CONNECT      	 false
#define BC_NO_BIND      	 true
#define BC_BIND      	 	 false

//
// Connection
//

struct DNSConnection
{
  int fd;
  struct sockaddr_in sa;
  int num;
  LINK(DNSConnection, link);
  EventIO eio;

  int connect(unsigned int ip, int port,
              bool non_blocking_connect = NON_BLOCKING_CONNECT,
              bool use_tcp = CONNECT_WITH_TCP, bool non_blocking = NON_BLOCKING, bool bind_random_port = BIND_ANY_PORT);

  int close();                  // 0 on success, -errno on failure

  virtual ~DNSConnection();
  DNSConnection();
};

#endif /*_P_DNSConnection_h*/
