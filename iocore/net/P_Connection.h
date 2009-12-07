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

  Connection.h
  Description:
  struct Connection
  struct Server
  struct ConnectionManager
 
  struct ConnectionManager
  ========================

  struct ConnectionManager provides the interface for network or disk
  connections.  There is a global ConnectionManager in the system
  (connectionManager).  

  Connection * connect() 
  Connection * accept() 

  The accept call is a blocking call while connect is non-blocking. They
  returns a new Connection instance which is an handle to the newly created
  connection. The connection `q instance can be used later for read/writes
  using an intance of IOProcessor class.

   
  **************************************************************************/

#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "inktomi++.h"

struct NetVCOptions;

//
// Defines
//

//#define NO_FD                    (-1)
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

///////////////////////////////////////////////////////////////////////
//
// Connection
//
///////////////////////////////////////////////////////////////////////
struct Connection
{
  SOCKET fd;
  struct sockaddr_in sa;

  int connect(unsigned int ip, int port,
              bool non_blocking_connect = NON_BLOCKING_CONNECT,
              bool use_tcp = CONNECT_WITH_TCP, bool non_blocking = NON_BLOCKING, bool bind_random_port = BIND_ANY_PORT);

  int fast_connect(const unsigned int ip, const int port, NetVCOptions * opt = NULL, const int socketFd = -1);

  int bind_connect(unsigned int target_ip, int target_port,
                   unsigned int my_ip,
                   NetVCOptions * opt = NULL,
                   bool non_blocking_connect = NON_BLOCKING_CONNECT,
                   bool use_tcp = CONNECT_WITH_TCP,
                   bool non_blocking = NON_BLOCKING, bool bc_no_connect = BC_CONNECT, bool bc_no_bind = BC_BIND);

  int setup_mc_send(unsigned int mc_ip, int mc_port,
                    unsigned int my_ip, int my_port,
                    bool non_blocking = NON_BLOCKING,
                    unsigned char mc_ttl = 1, bool mc_loopback = DISABLE_MC_LOOPBACK, Continuation * c = NULL);

  int setup_mc_receive(unsigned int mc_ip, int port,
                       bool non_blocking = NON_BLOCKING, Connection * sendchan = NULL, Continuation * c = NULL);

  int close();                  // 0 on success, -errno on failure

    virtual ~ Connection();
    Connection();
};

///////////////////////////////////////////////////////////////////////
//
// Server
//
///////////////////////////////////////////////////////////////////////
struct Server:Connection
{
  //
  // IP address in network byte order
  //
  unsigned int accept_ip;

  //
  // Use this call for the main proxy accept
  //
  int proxy_listen(bool non_blocking = false);

  int accept(Connection * c);

  //
  // Listen on a socket. We assume the port is in host by orderr, but
  // that the IP address (specified by accept_ip) has already been
  // converted into network byte order
  //

  int listen(int port, bool non_blocking = false, int recv_bufsize = 0, int send_bufsize = 0);
  int setup_fd_for_listen(bool non_blocking = false, int recv_bufsize = 0, int send_bufsize = 0);

    Server():Connection(), accept_ip(INADDR_ANY)
  {
  }
};

#endif /*_Connection_h*/
