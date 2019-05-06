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

#pragma once

#include "tscore/ink_platform.h"

struct NetVCOptions;

//
// Defines
//

#define NON_BLOCKING_CONNECT true
#define BLOCKING_CONNECT false
#define CONNECT_WITH_TCP true
#define CONNECT_WITH_UDP false
#define NON_BLOCKING true
#define BLOCKING false
#define BIND_RANDOM_PORT true
#define BIND_ANY_PORT false
#define ENABLE_MC_LOOPBACK true
#define DISABLE_MC_LOOPBACK false
#define BC_NO_CONNECT true
#define BC_CONNECT false
#define BC_NO_BIND true
#define BC_BIND false

///////////////////////////////////////////////////////////////////////
//
// Connection
//
///////////////////////////////////////////////////////////////////////
struct Connection {
  SOCKET fd;         ///< Socket for connection.
  IpEndpoint addr;   ///< Associated address.
  bool is_bound;     ///< Flag for already bound to a local address.
  bool is_connected; ///< Flag for already connected.
  int sock_type;

  /** Create and initialize the socket for this connection.

      A socket is created and the options specified by @a opt are
      set. The socket is @b not connected.

      @note It is important to pass the same @a opt to this method and
      @c connect.

      @return 0 on success, -ERRNO on failure.
      @see connect
  */
  int open(NetVCOptions const &opt = DEFAULT_OPTIONS ///< Socket options.
  );

  /** Connect the socket.

      The socket is connected to the remote @a addr and @a port. The
      @a opt structure is used to control blocking on the socket. All
      other options are set via @c open. It is important to pass the
      same @a opt to this method as was passed to @c open.

      @return 0 on success, -ERRNO on failure.
      @see open
  */
  int connect(sockaddr const *to,                       ///< Remote address and port.
              NetVCOptions const &opt = DEFAULT_OPTIONS ///< Socket options
  );

  /// Set the internal socket address struct.
  void
  setRemote(sockaddr const *remote_addr ///< Address and port.
  )
  {
    ats_ip_copy(&addr, remote_addr);
  }

  int setup_mc_send(sockaddr const *mc_addr, sockaddr const *my_addr, bool non_blocking = NON_BLOCKING, unsigned char mc_ttl = 1,
                    bool mc_loopback = DISABLE_MC_LOOPBACK, Continuation *c = nullptr);

  int setup_mc_receive(sockaddr const *from, sockaddr const *my_addr, bool non_blocking = NON_BLOCKING,
                       Connection *sendchan = nullptr, Continuation *c = nullptr);

  int close(); // 0 on success, -errno on failure

  void apply_options(NetVCOptions const &opt);

  virtual ~Connection();
  Connection();
  Connection(Connection const &that) = delete;

  /// Default options.
  static NetVCOptions const DEFAULT_OPTIONS;

  /**
   * Move control of the socket from the argument object orig to the current object.
   */
  void move(Connection &);

protected:
  /** Assignment operator.
   *
   * @param that Source object.
   * @return @a this
   *
   * This is protected because it is not safe in the general case, but is valid for
   * certain subclasses. Those provide a public assignemnt that depends on this method.
   */
  Connection &operator=(Connection const &that) = default;
  void _cleanup();
};

///////////////////////////////////////////////////////////////////////
//
// Server
//
///////////////////////////////////////////////////////////////////////
struct Server : public Connection {
  /// Client side (inbound) local IP address.
  IpEndpoint accept_addr;

  /// If set, a kernel HTTP accept filter
  bool http_accept_filter;

  int accept(Connection *c);

  //
  // Listen on a socket. We assume the port is in host by order, but
  // that the IP address (specified by accept_addr) has already been
  // converted into network byte order
  //

  int listen(bool non_blocking, const NetProcessor::AcceptOptions &opt);
  int setup_fd_for_listen(bool non_blocking, const NetProcessor::AcceptOptions &opt);

  Server() : Connection(), http_accept_filter(false) { ink_zero(accept_addr); }
};
