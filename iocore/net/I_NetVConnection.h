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

#include <string_view>
#include <optional>

#include "tscore/ink_inet.h"
#include "I_Action.h"
#include "I_VConnection.h"
#include "I_Event.h"
#include "tscore/List.h"
#include "I_IOBuffer.h"
#include "I_Socks.h"
#include "ts/apidefs.h"
#include "YamlSNIConfig.h"
#include "tscpp/util/TextView.h"
#include "tscore/IpMap.h"

#define CONNECT_SUCCESS 1
#define CONNECT_FAILURE 0

#define SSL_EVENT_SERVER 0
#define SSL_EVENT_CLIENT 1

// Indicator the context for a NetVConnection
typedef enum {
  NET_VCONNECTION_UNSET = 0,
  NET_VCONNECTION_IN,  // Client <--> ATS, Client-Side
  NET_VCONNECTION_OUT, // ATS <--> Server, Server-Side
} NetVConnectionContext_t;

/** Holds client options for NetVConnection.

    This class holds various options a user can specify for
    NetVConnection. Various clients need many slightly different
    features. This is an attempt to prevent out of control growth of
    the connection method signatures. Only options of interest need to
    be explicitly set -- the rest get sensible default values.

    @note Binding addresses is a bit complex. It is not currently
    possible to bind indiscriminately across protocols, which means
    any connection must commit to IPv4 or IPv6. For this reason the
    connection logic will look at the address family of @a local_addr
    even if @a addr_binding is @c ANY_ADDR and bind to any address in
    that protocol. If it's not an IP protocol, IPv4 will be used.
*/
struct NetVCOptions {
  typedef NetVCOptions self; ///< Self reference type.

  /// Values for valid IP protocols.
  enum ip_protocol_t {
    USE_TCP, ///< TCP protocol.
    USE_UDP  ///< UDP protocol.
  };

  /// IP (TCP or UDP) protocol to use on socket.
  ip_protocol_t ip_proto;

  /** IP address family.

      This is used for inbound connections only if @c local_ip is not
      set, which is sometimes more convenient for the client. This
      defaults to @c AF_INET so if the client sets neither this nor @c
      local_ip then IPv4 is used.

      For outbound connections this is ignored and the family of the
      remote address used.

      @note This is (inconsistently) called "domain" and "protocol" in
      other places. "family" is used here because that's what the
      standard IP data structures use.

  */
  uint16_t ip_family;

  /** The set of ways in which the local address should be bound.

      The protocol is set by the contents of @a local_addr regardless
      of this value. @c ANY_ADDR will override only the address.

      @note The difference between @c INTF_ADDR and @c FOREIGN_ADDR is
      whether transparency is enabled on the socket. It is the
      client's responsibility to set this correctly based on whether
      the address in @a local_addr is associated with an interface on
      the local system ( @c INTF_ADDR ) or is owned by a foreign
      system ( @c FOREIGN_ADDR ).  A binding style of @c ANY_ADDR
      causes the value in @a local_addr to be ignored.

      The IP address and port are separate because most clients treat
      these independently. For the same reason @c IpAddr is used
      to be clear that it contains no port data.

      @see local_addr
      @see addr_binding
   */
  enum addr_bind_style {
    ANY_ADDR,    ///< Bind to any available local address (don't care, default).
    INTF_ADDR,   ///< Bind to interface address in @a local_addr.
    FOREIGN_ADDR ///< Bind to foreign address in @a local_addr.
  };

  /** Local address for the connection.

      For outbound connections this must have the same family as the
      remote address (which is not stored in this structure). For
      inbound connections the family of this value overrides @a
      ip_family if set.

      @note Ignored if @a addr_binding is @c ANY_ADDR.
      @see addr_binding
      @see ip_family
  */
  IpAddr local_ip;

  /** Local port for connection.
      Set to 0 for "don't care" (default).
  */
  uint16_t local_port;

  /// How to bind the local address.
  /// @note Default is @c ANY_ADDR.
  addr_bind_style addr_binding;

  /// Make the socket blocking on I/O (default: @c false)
  bool f_blocking;
  /// Make socket block on connect (default: @c false)
  bool f_blocking_connect;

  // Use TCP Fast Open on this socket. The connect(2) call will be omitted.
  bool f_tcp_fastopen = false;

  /// Control use of SOCKS.
  /// Set to @c NO_SOCKS to disable use of SOCKS. Otherwise SOCKS is
  /// used if available.
  unsigned char socks_support;
  /// Version of SOCKS to use.
  unsigned char socks_version;

  int socket_recv_bufsize;
  int socket_send_bufsize;

  /// Configuration options for sockets.
  /// @note These are not identical to internal socket options but
  /// specifically defined for configuration. These are mask values
  /// and so must be powers of 2.
  uint32_t sockopt_flags;
  /// Value for TCP no delay for @c sockopt_flags.
  static uint32_t const SOCK_OPT_NO_DELAY = 1;
  /// Value for keep alive for @c sockopt_flags.
  static uint32_t const SOCK_OPT_KEEP_ALIVE = 2;
  /// Value for linger on for @c sockopt_flags
  static uint32_t const SOCK_OPT_LINGER_ON = 4;
  /// Value for TCP Fast open @c sockopt_flags
  static uint32_t const SOCK_OPT_TCP_FAST_OPEN = 8;

  uint32_t packet_mark;
  uint32_t packet_tos;

  EventType etype;

  /** Server name to use for SNI data on an outbound connection.
   */
  ats_scoped_str sni_servername;
  /** FQDN used to connect to the origin.  May be different
   * than sni_servername if pristine host headers are used
   */
  ats_scoped_str ssl_servername;

  /**
   * Client certificate to use in response to OS's certificate request
   */
  ats_scoped_str clientCertificate;
  /// Reset all values to defaults.

  uint8_t clientVerificationFlag = 0;
  void reset();

  void set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark = 0,
                      unsigned long _packet_tos = 0);

  NetVCOptions() { reset(); }
  ~NetVCOptions() {}
  /** Set the SNI server name.
      A local copy is made of @a name.
  */
  self &
  set_sni_servername(const char *name, size_t len)
  {
    IpEndpoint ip;

    // Literal IPv4 and IPv6 addresses are not permitted in "HostName".(rfc6066#section-3)
    if (name && len && ats_ip_pton(std::string_view(name, len), &ip) != 0) {
      sni_servername = ats_strndup(name, len);
    } else {
      sni_servername = nullptr;
    }
    return *this;
  }
  self &
  set_ssl_servername(const char *name)
  {
    if (name) {
      ssl_servername = ats_strdup(name);
    } else {
      ssl_servername = nullptr;
    }
    return *this;
  }
  self &
  set_client_certname(const char *name)
  {
    clientCertificate = ats_strdup(name);
    // clientCertificate = name;
    return *this;
  }

  self &
  operator=(self const &that)
  {
    if (&that != this) {
      /*
       * It is odd but necessary to null the scoped string pointer here
       * and then explicitly call release on them in the string assignements
       * below.
       * We a memcpy from that to this.  This will put that's string pointers into
       * this's memory.  Therefore we must first explicitly null out
       * this's original version of the string.  The release after the
       * memcpy removes the extra reference to that's copy of the string
       * Removing the release will eventualy cause a double free crash
       */
      sni_servername    = nullptr; // release any current name.
      ssl_servername    = nullptr;
      clientCertificate = nullptr;
      memcpy(static_cast<void *>(this), &that, sizeof(self));
      if (that.sni_servername) {
        sni_servername.release(); // otherwise we'll free the source string.
        this->sni_servername = ats_strdup(that.sni_servername);
      }
      if (that.ssl_servername) {
        ssl_servername.release(); // otherwise we'll free the source string.
        this->ssl_servername = ats_strdup(that.ssl_servername);
      }
      if (that.clientCertificate) {
        clientCertificate.release(); // otherwise we'll free the source string.
        this->clientCertificate = ats_strdup(that.clientCertificate);
      }
    }
    return *this;
  }

  std::string_view get_family_string() const;

  std::string_view get_proto_string() const;

  /// @name Debugging
  //@{
  /// Convert @a s to its string equivalent.
  static const char *toString(addr_bind_style s);
  //@}

  // noncopyable
  NetVCOptions(const NetVCOptions &) = delete;
};

/**
  A VConnection for a network socket. Abstraction for a net connection.
  Similar to a socket descriptor VConnections are IO handles to
  streams. In one sense, they serve a purpose similar to file
  descriptors. Unlike file descriptors, VConnections allow for a
  stream IO to be done based on a single read or write call.

*/
class NetVConnection : public AnnotatedVConnection
{
public:
  // How many bytes have been queued to the OS for sending by haven't been sent yet
  // Not all platforms support this, and if they don't we'll return -1 for them
  virtual int64_t
  outstanding()
  {
    return -1;
  };

  /**
     Initiates read. Thread safe, may be called when not handling
     an event from the NetVConnection, or the NetVConnection creation
     callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr><td>c->handleEvent(VC_EVENT_READ_READY, vio)</td><td>data added to buffer</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_READ_COMPLETE, vio)</td><td>finished reading nbytes of data</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_EOS, vio)</td><td>the stream has been shutdown</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_ERROR, vio)</td><td>error</td></tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_read(). The vio can be changed only during call backs
    from the vconnection.

    @param c continuation to be called back after (partial) read
    @param nbytes no of bytes to read, if unknown set to INT64_MAX
    @param buf buffer to put the data into
    @return vio

  */
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override = 0;

  /**
    Initiates write. Thread-safe, may be called when not handling
    an event from the NetVConnection, or the NetVConnection creation
    callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_READY, vio)</td>
        <td>signifies data has written from the reader or there are no bytes available for the reader to write.</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_COMPLETE, vio)</td>
        <td>signifies the amount of data indicated by nbytes has been read from the buffer</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_ERROR, vio)</td>
        <td>signified that error occured during write.</td>
      </tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_write(). The vio can be changed only during call backs
    from the vconnection. The vconnection deallocates the reader
    when it is destroyed.

    @param c continuation to be called back after (partial) write
    @param nbytes no of bytes to write, if unknown msut be set to INT64_MAX
    @param buf source of data
    @param owner
    @return vio pointer

  */
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override = 0;

  /**
    Closes the vconnection. A state machine MUST call do_io_close()
    when it has finished with a VConenction. do_io_close() indicates
    that the VConnection can be deallocated. After a close has been
    called, the VConnection and underlying processor must NOT send
    any more events related to this VConnection to the state machine.
    Likeswise, state machine must not access the VConnectuion or
    any returned VIOs after calling close. lerrno indicates whether
    a close is a normal close or an abort. The difference between
    a normal close and an abort depends on the underlying type of
    the VConnection. Passing VIO::CLOSE for lerrno indicates a
    normal close while passing VIO::ABORT indicates an abort.

    @param lerrno VIO:CLOSE for regular close or VIO::ABORT for aborts

  */
  void do_io_close(int lerrno = -1) override = 0;

  /**
    Shuts down read side, write side, or both. do_io_shutdown() can
    be used to terminate one or both sides of the VConnection. The
    howto is one of IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE,
    IO_SHUTDOWN_READWRITE. Once a side of a VConnection is shutdown,
    no further I/O can be done on that side of the connections and
    the underlying processor MUST NOT send any further events
    (INCLUDING TIMOUT EVENTS) to the state machine. The state machine
    MUST NOT use any VIOs from a shutdown side of a connection.
    Even if both sides of a connection are shutdown, the state
    machine MUST still call do_io_close() when it wishes the
    VConnection to be deallocated.

    @param howto IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE, IO_SHUTDOWN_READWRITE

  */
  void do_io_shutdown(ShutdownHowTo_t howto) override = 0;

  /**
    Sends out of band messages over the connection. This function
    is used to send out of band messages (is this still useful?).
    cont is called back with VC_EVENT_OOB_COMPLETE - on successful
    send or VC_EVENT_EOS - if the other side has shutdown the
    connection. These callbacks could be re-entrant. Only one
    send_OOB can be in progress at any time for a VC.

    @param cont to be called back with events.
    @param buf message buffer.
    @param len length of the message.

  */
  virtual Action *send_OOB(Continuation *cont, char *buf, int len);

  /**
    Cancels a scheduled send_OOB. Part of the message could have
    been sent already. Not callbacks to the cont are made after
    this call. The Action returned by send_OOB should not be accessed
    after cancel_OOB.

  */
  virtual void cancel_OOB();

  ////////////////////////////////////////////////////////////
  // Set the timeouts associated with this connection.      //
  // active_timeout is for the total elasped time of        //
  // the connection.                                        //
  // inactivity_timeout is the elapsed time from the time   //
  // a read or a write was scheduled during which the       //
  // connection  was unable to sink/provide data.           //
  // calling these functions repeatedly resets the timeout. //
  // These functions are NOT THREAD-SAFE, and may only be   //
  // called when handing an  event from this NetVConnection,//
  // or the NetVConnection creation callback.               //
  ////////////////////////////////////////////////////////////

  /**
    Sets time after which SM should be notified.

    Sets the amount of time (in nanoseconds) after which the state
    machine using the NetVConnection should receive a
    VC_EVENT_ACTIVE_TIMEOUT event. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. The timer is reset if the function is
    called repeatedly This call can be used by SMs to make sure
    that it does not keep any connections open for a really long
    time.

    Timeout symantics:

    Should a timeout occur, the state machine for the read side of
    the NetVConnection is signaled first assuming that a read has
    been initiated on the NetVConnection and that the read side of
    the NetVConnection has not been shutdown. Should either of the
    two conditions not be met, the NetProcessor will attempt to
    signal the write side. If a timeout is sent to the read side
    state machine and its handler, return EVENT_DONE, a timeout
    will not be sent to the write side. Should the return from the
    handler not be EVENT_DONE and the write side state machine is
    different (in terms of pointer comparison) from the read side
    state machine, the NetProcessor will try to signal the write
    side state machine as well. To signal write side, a write must
    have been initiated on it and the write must not have been
    shutdown.

    Receiving a timeout is only a notification that the timer has
    expired. The NetVConnection is still usable. Further timeouts
    of the type signaled will not be generated unless the timeout
    is reset via the set_active_timeout() or set_inactivity_timeout()
    interfaces.

  */
  virtual void set_active_timeout(ink_hrtime timeout_in) = 0;

  /**
    Sets time after which SM should be notified if the requested
    IO could not be performed. Sets the amount of time (in nanoseconds),
    if the NetVConnection is idle on both the read or write side,
    after which the state machine using the NetVConnection should
    receive a VC_EVENT_INACTIVITY_TIMEOUT event. Either read or
    write traffic will cause timer to be reset. Calling this function
    again also resets the timer. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. See section on timeout semantics above.

   */
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;

  /**
    Clears the active timeout. No active timeouts will be sent until
    set_active_timeout() is used to reset the active timeout.

  */
  virtual void cancel_active_timeout() = 0;

  /**
    Clears the inactivity timeout. No inactivity timeouts will be
    sent until set_inactivity_timeout() is used to reset the
    inactivity timeout.

  */
  virtual void cancel_inactivity_timeout() = 0;

  /** Set the action to use a continuation.
      The action continuation will be called with an event if there is no pending I/O operation
      to receive the event.

      Pass @c nullptr to disable.

      @internal Subclasses should implement this if they support actions. This abstract class does
      not. If the subclass doesn't have an action this method is silently ignored.
  */
  virtual void
  set_action(Continuation *)
  {
    return;
  }

  virtual void add_to_keep_alive_queue() = 0;

  virtual void remove_from_keep_alive_queue() = 0;

  virtual bool add_to_active_queue() = 0;

  /** @return the current active_timeout value in nanosecs */
  virtual ink_hrtime get_active_timeout() = 0;

  /** @return current inactivity_timeout value in nanosecs */
  virtual ink_hrtime get_inactivity_timeout() = 0;

  /** Force an @a event if a write operation empties the write buffer.

      This event will be sent to the VIO, the same place as other IO events.
      Use an @a event value of 0 to cancel the trap.

      The event is sent only the next time the write buffer is emptied, not
      every future time. The event is sent only if otherwise no event would
      be generated.
   */
  virtual void trapWriteBufferEmpty(int event = VC_EVENT_WRITE_READY);

  /** Returns local sockaddr storage. */
  sockaddr const *get_local_addr();

  /** Returns local ip.
      @deprecated get_local_addr() should be used instead for AF_INET6 compatibility.
  */

  in_addr_t get_local_ip();

  /** Returns local port. */
  uint16_t get_local_port();

  /** Returns remote sockaddr storage. */
  sockaddr const *get_remote_addr();
  IpEndpoint const &get_remote_endpoint();

  /** Returns remote ip.
      @deprecated get_remote_addr() should be used instead for AF_INET6 compatibility.
  */
  in_addr_t get_remote_ip();

  /** Returns remote port. */
  uint16_t get_remote_port();

  /** Set the context of NetVConnection.
   * The context is ONLY set once and will not be changed.
   *
   * @param context The context to be set.
   */
  void
  set_context(NetVConnectionContext_t context)
  {
    ink_assert(NET_VCONNECTION_UNSET == netvc_context);
    netvc_context = context;
  }

  /** Get the context.
   * @return the context of current NetVConnection
   */
  NetVConnectionContext_t
  get_context() const
  {
    return netvc_context;
  }

  /** Structure holding user options. */
  NetVCOptions options;

  /** Attempt to push any changed options down */
  virtual void apply_options() = 0;

  //
  // Private
  //

  // The following variable used to obtain host addr when transparency
  // is enabled by SocksProxy
  SocksAddrType socks_addr;

  unsigned int attributes;
  EThread *thread;

  /// PRIVATE: The public interface is VIO::reenable()
  void reenable(VIO *vio) override = 0;

  /// PRIVATE: The public interface is VIO::reenable()
  void reenable_re(VIO *vio) override = 0;

  /// PRIVATE
  ~NetVConnection() override {}
  /**
    PRIVATE: instances of NetVConnection cannot be created directly
    by the state machines. The objects are created by NetProcessor
    calls like accept connect_re() etc. The constructor is public
    just to avoid compile errors.

  */
  NetVConnection();

  virtual SOCKET get_socket() = 0;

  /** Set the TCP initial congestion window */
  virtual int set_tcp_init_cwnd(int init_cwnd) = 0;

  /** Set the TCP congestion control algorithm */
  virtual int set_tcp_congestion_control(int side) = 0;

  /** Set local sock addr struct. */
  virtual void set_local_addr() = 0;

  /** Set remote sock addr struct. */
  virtual void set_remote_addr() = 0;

  /** Set remote sock addr struct. */
  virtual void set_remote_addr(const sockaddr *) = 0;

  /** Set the MPTCP state for this connection */
  virtual void set_mptcp_state() = 0;

  // for InkAPI
  bool
  get_is_internal_request() const
  {
    return is_internal_request;
  }

  void
  set_is_internal_request(bool val = false)
  {
    is_internal_request = val;
  }

  /// Get the transparency state.
  bool
  get_is_transparent() const
  {
    return is_transparent;
  }

  /// Get the MPTCP state of the VC.
  std::optional<bool>
  get_mptcp_state() const
  {
    return mptcp_state;
  }

  /// Set the transparency state.
  void
  set_is_transparent(bool state = true)
  {
    is_transparent = state;
  }

  /// Get the proxy protocol enabled flag
  bool
  get_is_proxy_protocol() const
  {
    return is_proxy_protocol;
  }
  /// Set the proxy protocol enabled flag on the port
  void
  set_is_proxy_protocol(bool state = true)
  {
    is_proxy_protocol = state;
  }

  virtual int
  populate_protocol(std::string_view *results, int n) const
  {
    return 0;
  }

  virtual const char *
  protocol_contains(std::string_view prefix) const
  {
    return nullptr;
  }

  // noncopyable
  NetVConnection(const NetVConnection &) = delete;
  NetVConnection &operator=(const NetVConnection &) = delete;

  enum class ProxyProtocolVersion {
    UNDEFINED,
    V1,
    V2,
  };

  enum class ProxyProtocolData {
    UNDEFINED,
    SRC,
    DST,
  };

  int
  set_proxy_protocol_addr(const ProxyProtocolData src_or_dst, ts::TextView &ip_addr_str)
  {
    int ret = -1;

    if (src_or_dst == ProxyProtocolData::SRC) {
      ret = ats_ip_pton(ip_addr_str, &pp_info.src_addr);
    } else {
      ret = ats_ip_pton(ip_addr_str, &pp_info.dst_addr);
    }
    return ret;
  }

  int
  set_proxy_protocol_src_addr(ts::TextView src)
  {
    return set_proxy_protocol_addr(ProxyProtocolData::SRC, src);
  }

  int
  set_proxy_protocol_dst_addr(ts::TextView src)
  {
    return set_proxy_protocol_addr(ProxyProtocolData::DST, src);
  }

  int
  set_proxy_protocol_port(const ProxyProtocolData src_or_dst, in_port_t port)
  {
    if (src_or_dst == ProxyProtocolData::SRC) {
      pp_info.src_addr.port() = htons(port);
    } else {
      pp_info.dst_addr.port() = htons(port);
    }
    return port;
  }

  int
  set_proxy_protocol_src_port(in_port_t port)
  {
    return set_proxy_protocol_port(ProxyProtocolData::SRC, port);
  }

  int
  set_proxy_protocol_dst_port(in_port_t port)
  {
    return set_proxy_protocol_port(ProxyProtocolData::DST, port);
  }

  void
  set_proxy_protocol_version(const ProxyProtocolVersion ver)
  {
    pp_info.proxy_protocol_version = ver;
  }

  ProxyProtocolVersion
  get_proxy_protocol_version()
  {
    return pp_info.proxy_protocol_version;
  }

  sockaddr const *get_proxy_protocol_addr(const ProxyProtocolData);

  sockaddr const *
  get_proxy_protocol_src_addr()
  {
    return get_proxy_protocol_addr(ProxyProtocolData::SRC);
  }

  uint16_t
  get_proxy_protocol_src_port()
  {
    return ats_ip_port_host_order(this->get_proxy_protocol_addr(ProxyProtocolData::SRC));
  }

  sockaddr const *
  get_proxy_protocol_dst_addr()
  {
    return get_proxy_protocol_addr(ProxyProtocolData::DST);
  }

  uint16_t
  get_proxy_protocol_dst_port()
  {
    return ats_ip_port_host_order(this->get_proxy_protocol_addr(ProxyProtocolData::DST));
  };

  typedef struct _ProxyProtocol {
    ProxyProtocolVersion proxy_protocol_version = ProxyProtocolVersion::UNDEFINED;
    uint16_t ip_family;
    IpEndpoint src_addr;
    IpEndpoint dst_addr;
  } ProxyProtocol;

  ProxyProtocol pp_info;

protected:
  IpEndpoint local_addr;
  IpEndpoint remote_addr;

  bool got_local_addr;
  bool got_remote_addr;

  bool is_internal_request;
  /// Set if this connection is transparent.
  bool is_transparent;
  /// Set if proxy protocol is enabled
  bool is_proxy_protocol;
  /// This is essentially a tri-state, we leave it undefined to mean no MPTCP support
  std::optional<bool> mptcp_state;
  /// Set if the next write IO that empties the write buffer should generate an event.
  int write_buffer_empty_event;
  /// NetVConnection Context.
  NetVConnectionContext_t netvc_context;
};

inline NetVConnection::NetVConnection()
  : AnnotatedVConnection(nullptr),
    attributes(0),
    thread(nullptr),
    got_local_addr(false),
    got_remote_addr(false),
    is_internal_request(false),
    is_transparent(false),
    is_proxy_protocol(false),
    write_buffer_empty_event(0),
    netvc_context(NET_VCONNECTION_UNSET)
{
  ink_zero(local_addr);
  ink_zero(remote_addr);
}

inline void
NetVConnection::trapWriteBufferEmpty(int event)
{
  write_buffer_empty_event = event;
}
