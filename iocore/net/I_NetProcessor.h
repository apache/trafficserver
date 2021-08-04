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

#include "tscore/IpMap.h"
#include "I_EventSystem.h"
#include "I_Socks.h"
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
  struct AcceptOptions {
    typedef AcceptOptions self; ///< Self reference type.

    /// Port on which to listen.
    /// 0 => don't care, which is useful if the socket is already bound.
    int local_port;
    /// Local address to bind for accept.
    /// If not set -> any address.
    IpAddr local_ip;
    /// IP address family.
    /// @note Ignored if an explicit incoming address is set in the
    /// the configuration (@c local_ip). If neither is set IPv4 is used.
    int ip_family;
    /// Should we use accept threads? If so, how many?
    int accept_threads;
    /// Event type to generate on accept.
    EventType etype;
    /** If @c true, the continuation is called back with
        @c NET_EVENT_ACCEPT_SUCCEED
        or @c NET_EVENT_ACCEPT_FAILED on success and failure resp.
    */

    bool localhost_only;
    /// Are frequent accepts expected?
    /// Default: @c false.
    bool frequent_accept;
    bool backdoor;

    /// Socket receive buffer size.
    /// 0 => OS default.
    int recv_bufsize;
    /// Socket transmit buffer size.
    /// 0 => OS default.
    int send_bufsize;
    /// defer accept for @c sockopt.
    /// 0 => OS default.
    int defer_accept;
    /// Socket options for @c sockopt.
    /// 0 => do not set options.
    uint32_t sockopt_flags;
    uint32_t packet_mark;
    uint32_t packet_tos;

    int tfo_queue_length;

    /** Transparency on client (user agent) connection.
        @internal This is irrelevant at a socket level (since inbound
        transparency must be set up when the listen socket is created)
        but it's critical that the connection handling logic knows
        whether the inbound (client / user agent) connection is
        transparent.
    */
    bool f_inbound_transparent;

    /** MPTCP enabled on listener.
        @internal For logging and metrics purposes to know whether the
        listener enabled MPTCP or not.
    */
    bool f_mptcp;

    /// Proxy Protocol enabled
    bool f_proxy_protocol;

    /// Default constructor.
    /// Instance is constructed with default values.
    AcceptOptions() { this->reset(); }
    /// Reset all values to defaults.
    self &reset();
  };

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
  inkcoreapi virtual Action *accept(Continuation *cont, AcceptOptions const &opt = DEFAULT_ACCEPT_OPTIONS);

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
  virtual Action *main_accept(Continuation *cont, SOCKET listen_socket_in, AcceptOptions const &opt = DEFAULT_ACCEPT_OPTIONS);
  virtual void stop_accept();

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

  inkcoreapi Action *connect_re(Continuation *cont, sockaddr const *addr, NetVCOptions *options = nullptr);

  /**
    Initializes the net processor. This must be called before the event threads are started.

  */
  virtual void init() = 0;

  virtual void init_socks() = 0;

  inkcoreapi virtual NetVConnection *allocate_vc(EThread *) = 0;

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
  NetProcessor(const NetProcessor &) = delete;
  NetProcessor &operator=(const NetProcessor &) = delete;

private:
  /** @note Not implemented. */
  virtual int
  stop()
  {
    ink_release_assert(!"NetProcessor::stop not implemented");
    return 1;
  }
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
extern inkcoreapi NetProcessor &netProcessor;

/**
  Global netProcessor singleton object for making ssl enabled net
  calls. As far as the SM is concerned this behaves exactly like
  netProcessor. The only difference is that the connections are
  over ssl.

*/
extern inkcoreapi NetProcessor &sslNetProcessor;
extern inkcoreapi NetProcessor &quicNetProcessor;
