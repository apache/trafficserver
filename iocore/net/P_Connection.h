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
  SOCKET fd; ///< Socket for connection.
  struct sockaddr_in sa; ///< Remote address.
  bool is_bound; ///< Flag for already bound to a local address.
  bool is_connected; ///< Flag for already connected.

  /** Create and initialize the socket for this connection.

      A socket is created and the options specified by @a opt are
      set. The socket is @b not connected.

      @note It is important to pass the same @a opt to this method and
      @c connect.

      @return 0 on success, -ERRNO on failure.
      @see connect
  */
  int open(
	   NetVCOptions const& opt = DEFAULT_OPTIONS ///< Socket options.
	   );

  /** Connect the socket.

      The socket is connected to the remote @a addr and @a port. The
      @a opt structure is used to control blocking on the socket. All
      other options are set via @c open. It is important to pass the
      same @a opt to this method as was passed to @c open.

      @return 0 on success, -ERRNO on failure.
      @see open
  */
  int connect(
	   uint32 addr, ///< Remote address.
	   uint16 port, ///< Remote port.
	   NetVCOptions const& opt = DEFAULT_OPTIONS ///< Socket options
	   );


  /// Set the internal socket address struct.
  /// @internal Used only by ICP.
  void setRemote(
		 uint32 addr, ///< Remote IP address.
		 uint16 port ///< Remote port.
	     ) {
    sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(&sa);
    sa.sin_family = AF_INET;
    sa_in->sin_port = htons(port);
    sa_in->sin_addr.s_addr = addr;
    memset(&(sa_in->sin_zero), 0, 8);
  }
    
  int setup_mc_send(unsigned int mc_ip, int mc_port,
                    unsigned int my_ip, int my_port,
                    bool non_blocking = NON_BLOCKING,
                    unsigned char mc_ttl = 1, bool mc_loopback = DISABLE_MC_LOOPBACK, Continuation * c = NULL);

  int setup_mc_receive(unsigned int mc_ip, int port,
                       bool non_blocking = NON_BLOCKING, Connection * sendchan = NULL, Continuation * c = NULL);

  int close();                  // 0 on success, -errno on failure

  virtual ~ Connection();
  Connection();

  /// Default options.
  static NetVCOptions const DEFAULT_OPTIONS;

protected:
  void _cleanup();
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

  /// If set, transparently connect to origin server for requests.
  bool f_outbound_transparent;
  /// If set, the related incoming connect was transparent.
  bool f_inbound_transparent;

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

  Server()
    : Connection()
    , accept_ip(INADDR_ANY)
    , f_outbound_transparent(false)
  { }
};

#endif /*_Connection_h*/
