/** @file

  This file implements an I/O Processor for network I/O

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

#include "iocore/eventsystem/EventSystem.h"
#include "iocore/net/Socks.h"
#include "iocore/net/NetVConnection.h"
#include "iocore/net/AcceptOptions.h"
struct socks_conf_struct;
#define NET_CONNECT_TIMEOUT 30

struct NetVCOptions;

/**
  This is the heart of the Net system. Provides common network APIs,
  like accept, connect etc. It performs network I/O on behalf of a
  state machine.

*/
class NetProcessor : public Processor
{
public:
  /** Options for @c accept.
   */
  using AcceptOptions = ::AcceptOptions;

  /**
    Accept connections on a port.

    Callbacks:
      - cont->handleEvent( NET_EVENT_ACCEPT, NetVConnection *) is
        called for each new connection
      - cont->handleEvent(EVENT_ERROR,-errno) on a bad error

    Re-entrant callbacks (based on callback_on_open flag):
      - cont->handleEvent(NET_EVENT_ACCEPT_SUCCEED, 0) on successful
        accept init
      - cont->handleEvent(NET_EVENT_ACCEPT_FAILED, 0) on accept
        init failure

    @param cont Continuation to be called back with events this
      continuation is not locked on callbacks and so the handler must
      be re-entrant.
    @param opt Accept options.
    @return Action, that can be cancelled to cancel the accept. The
      port becomes free immediately.
   */
  virtual Action *accept(Continuation *cont, AcceptOptions const &opt = DEFAULT_ACCEPT_OPTIONS) = 0;

  /**
    Accepts incoming connections on port. Accept connections on port.
    Accept is done on all net threads and throttle limit is imposed
    if frequent_accept flag is true. This is similar to the accept
    method described above. The only difference is that the list
    of parameter that is takes is limited.

    Callbacks:
      - cont->handleEvent( NET_EVENT_ACCEPT, NetVConnection *) is called for each new connection
      - cont->handleEvent(EVENT_ERROR,-errno) on a bad error

    Re-entrant callbacks (based on callback_on_open flag):
      - cont->handleEvent(NET_EVENT_ACCEPT_SUCCEED, 0) on successful accept init
      - cont->handleEvent(NET_EVENT_ACCEPT_FAILED, 0) on accept init failure

    @param cont Continuation to be called back with events this
      continuation is not locked on callbacks and so the handler must
      be re-entrant.
    @param listen_socket_in if passed, used for listening.
    @param opt Accept options.
    @return Action, that can be cancelled to cancel the accept. The
      port becomes free immediately.

  */
  virtual Action *main_accept(Continuation *cont, SOCKET listen_socket_in, AcceptOptions const &opt = DEFAULT_ACCEPT_OPTIONS) = 0;

  virtual void stop_accept() = 0;

  /**
    Open a NetVConnection for connection oriented I/O. Connects
    through sockserver if netprocessor is configured to use socks
    or is socks parameters to the call are set.

    Re-entrant callbacks:
      - On success calls: c->handleEvent(NET_EVENT_OPEN, NetVConnection *)
      - On failure calls: c->handleEvent(NET_EVENT_OPEN_FAILED, -errno)

    @note Connection may not have been established when cont is
      call back with success. If this behaviour is desired use
      synchronous connect connet_s method.

    @param cont Continuation to be called back with events.
    @param addr target address and port to connect to.
    @param options @see NetVCOptions.

  */
  virtual Action *connect_re(Continuation *cont, sockaddr const *addr, NetVCOptions const &options) = 0;

  /**
    Initializes the net processor. This must be called before the event threads are started.

  */
  virtual void init() = 0;

  virtual void init_socks() = 0;

  virtual NetVConnection *allocate_vc(EThread *) = 0;

  /** Private constructor. */
  NetProcessor(){};

  /** Private destructor. */
  ~NetProcessor() override{};

  /** This is MSS for connections we accept (client connections). */
  static int accept_mss;

  //
  // The following are required by the SOCKS protocol:
  //
  // Either the configuration variables will give your a regular
  // expression for all the names that are to go through the SOCKS
  // server, or will give you a list of domain names which should *not* go
  // through SOCKS. If the SOCKS option is set to false then, these
  // variables (regular expression or list) should be set
  // appropriately. If it is set to TRUE then, in addition to supplying
  // the regular expression or the list, the user should also give the
  // the ip address and port number for the SOCKS server (use
  // appropriate defaults)

  /* shared by regular netprocessor and ssl netprocessor */
  static socks_conf_struct *socks_conf_stuff;

  /// Default options instance.
  static AcceptOptions const DEFAULT_ACCEPT_OPTIONS;

  // noncopyable
  NetProcessor(const NetProcessor &)            = delete;
  NetProcessor &operator=(const NetProcessor &) = delete;
};

/**
  Global NetProcessor singleton object for making net calls. All
  net processor calls like connect, accept, etc are made using this
  object.

  @code
    netProcessor.accept(my_cont, ...);
    netProcessor.connect_re(my_cont, ...);
  @endcode

*/
extern NetProcessor &netProcessor;

/**
  Global netProcessor singleton object for making ssl enabled net
  calls. As far as the SM is concerned this behaves exactly like
  netProcessor. The only difference is that the connections are
  over ssl.

*/
extern NetProcessor &sslNetProcessor;
extern NetProcessor &quicNetProcessor;
