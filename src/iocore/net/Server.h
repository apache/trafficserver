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

#include "iocore/net/NetProcessor.h"
#include "P_Connection.h"

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_inet.h"
#include "tscore/ink_memory.h"

struct Server {
  /// Client side (inbound) local IP address.
  IpEndpoint accept_addr;
  /// Associated address.
  IpEndpoint addr;

  /// If set, a kernel HTTP accept filter
  bool http_accept_filter = false;

  UnixSocket sock{NO_SOCK};

  int accept(Connection *c);

  int close();

  //
  // Listen on a socket. We assume the port is in host by order, but
  // that the IP address (specified by accept_addr) has already been
  // converted into network byte order
  //

  int listen(bool non_blocking, const NetProcessor::AcceptOptions &opt);
  int setup_fd_for_listen(bool non_blocking, const NetProcessor::AcceptOptions &opt);
  int setup_fd_after_listen(const NetProcessor::AcceptOptions &opt);

  Server() { ink_zero(accept_addr); }
};
