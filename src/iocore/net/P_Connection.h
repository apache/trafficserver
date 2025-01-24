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
  using an instance of IOProcessor class.


  **************************************************************************/

#pragma once

#include "iocore/net/Net.h"
#include "iocore/net/NetProcessor.h"

#include "iocore/eventsystem/UnixSocket.h"

struct NetVCOptions;

///////////////////////////////////////////////////////////////////////
//
// Connection
//
///////////////////////////////////////////////////////////////////////
struct Connection {
  UnixSocket sock{NO_FD};
  IpEndpoint addr;                 ///< Associated address.
  bool       is_bound     = false; ///< Flag for already bound to a local address.
  bool       is_connected = false; ///< Flag for already connected.
  int        sock_type    = 0;

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
  int connect(sockaddr const     *to,                   ///< Remote address and port.
              NetVCOptions const &opt = DEFAULT_OPTIONS ///< Socket options
  );

  /// Set the internal socket address struct.
  void
  setRemote(sockaddr const *remote_addr ///< Address and port.
  )
  {
    ats_ip_copy(&addr, remote_addr);
  }

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
   * certain subclasses. Those provide a public assignment that depends on this method.
   */
  Connection &operator=(Connection const &that) = default;
  void        _cleanup();
};
