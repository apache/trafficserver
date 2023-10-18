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

#include "NetVCOptions.h"
#include "ProxyProtocol.h"
#include "Net.h"

#include <string_view>
#include <optional>

#include "tscore/ink_inet.h"
#include "Action.h"
#include "VConnection.h"
#include "Event.h"
#include "tscore/List.h"
#include "IOBuffer.h"
#include "Socks.h"
#include "ts/apidefs.h"
#include "YamlSNIConfig.h"
#include "swoc/TextView.h"

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

/**
  A VConnection for a network socket. Abstraction for a net connection.
  Similar to a socket descriptor VConnections are IO handles to
  streams. In one sense, they serve a purpose similar to file
  descriptors. Unlike file descriptors, VConnections allow for a
  stream IO to be done based on a single read or write call.

*/
class NetVConnection : public VConnection, public PluginUserArgs<TS_USER_ARGS_VCONN>
{
public:
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
        <td>signified that error occurred during write.</td>
      </tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_write(). The vio can be changed only during call backs
    from the vconnection. The vconnection deallocates the reader
    when it is destroyed.

    @param c continuation to be called back after (partial) write
    @param nbytes no of bytes to write, if unknown must be set to INT64_MAX
    @param buf source of data
    @param owner
    @return vio pointer

  */
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override = 0;

  /**
    Closes the vconnection. A state machine MUST call do_io_close()
    when it has finished with a VConnection. do_io_close() indicates
    that the VConnection can be deallocated. After a close has been
    called, the VConnection and underlying processor must NOT send
    any more events related to this VConnection to the state machine.
    Likewise, state machine must not access the VConnection or
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
    (INCLUDING TIMEOUT EVENTS) to the state machine. The state machine
    MUST NOT use any VIOs from a shutdown side of a connection.
    Even if both sides of a connection are shutdown, the state
    machine MUST still call do_io_close() when it wishes the
    VConnection to be deallocated.

    @param howto IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE, IO_SHUTDOWN_READWRITE

  */
  void do_io_shutdown(ShutdownHowTo_t howto) override = 0;

  /**
    Return the server name that is appropriate for the network VC type
  */
  virtual const char *
  get_server_name() const
  {
    return nullptr;
  }

  ////////////////////////////////////////////////////////////
  // Set the timeouts associated with this connection.      //
  // active_timeout is for the total elapsed time of        //
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

    Timeout semantics:

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
  virtual void set_inactivity_timeout(ink_hrtime timeout_in)         = 0;
  virtual void set_default_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual bool is_default_inactivity_timeout()                       = 0;

  /**
    Clears the active timeout. No active timeouts will be sent until
    set_active_timeout() is used to reset the active timeout.

  */
  virtual void cancel_active_timeout() = 0;

  /**
    Clears the inactivity timeout. No inactivity timeouts will be sent (aside
    from the default inactivity timeout) until set_inactivity_timeout() is used
    to reset the inactivity timeout.

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
  IpEndpoint const &get_local_endpoint();

  /** Returns local port. */
  uint16_t get_local_port();

  /** Returns remote sockaddr storage. */
  sockaddr const *get_remote_addr();
  IpEndpoint const &get_remote_endpoint();

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

  /**
   * Returns true if the network protocol
   * supports a client provided SNI value
   */
  virtual bool
  support_sni() const
  {
    return false;
  }

  virtual const char *
  get_sni_servername() const
  {
    return nullptr;
  }

  virtual bool
  peer_provided_cert() const
  {
    return false;
  }

  virtual int
  provided_cert() const
  {
    return 0;
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

  unsigned int attributes = 0;
  EThread *thread         = nullptr;

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

  bool
  get_is_unmanaged_request() const
  {
    return is_unmanaged_request;
  }

  void
  set_is_unmanaged_request(bool val = false)
  {
    is_unmanaged_request = val;
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
  NetVConnection(const NetVConnection &)            = delete;
  NetVConnection &operator=(const NetVConnection &) = delete;

  ProxyProtocolVersion
  get_proxy_protocol_version() const
  {
    return pp_info.version;
  }

  sockaddr const *get_proxy_protocol_addr(const ProxyProtocolData) const;

  sockaddr const *
  get_proxy_protocol_src_addr() const
  {
    return get_proxy_protocol_addr(ProxyProtocolData::SRC);
  }

  uint16_t
  get_proxy_protocol_src_port() const
  {
    return ats_ip_port_host_order(this->get_proxy_protocol_addr(ProxyProtocolData::SRC));
  }

  sockaddr const *
  get_proxy_protocol_dst_addr() const
  {
    return get_proxy_protocol_addr(ProxyProtocolData::DST);
  }

  uint16_t
  get_proxy_protocol_dst_port() const
  {
    return ats_ip_port_host_order(this->get_proxy_protocol_addr(ProxyProtocolData::DST));
  };

  void set_proxy_protocol_info(const ProxyProtocol &src);
  const ProxyProtocol &get_proxy_protocol_info() const;

  bool has_proxy_protocol(IOBufferReader *);
  bool has_proxy_protocol(char *, int64_t *);

  template <typename S> S *get_service() const;

protected:
  enum class Service : uint8_t {
    TLS_ALPN,
    TLS_Basic,
    TLS_CertSwitch,
    TLS_EarlyData,
    TLS_SNI,
    TLS_SessionResumption,
    TLS_Tunnel,
    QUIC,
    N_SERVICES,
  };

  IpEndpoint local_addr;
  IpEndpoint remote_addr;
  ProxyProtocol pp_info;

  bool got_local_addr  = false;
  bool got_remote_addr = false;

  bool is_internal_request  = false;
  bool is_unmanaged_request = false;
  /// Set if this connection is transparent.
  bool is_transparent = false;
  /// Set if proxy protocol is enabled
  bool is_proxy_protocol = false;
  /// This is essentially a tri-state, we leave it undefined to mean no MPTCP support
  std::optional<bool> mptcp_state;
  /// Set if the next write IO that empties the write buffer should generate an event.
  int write_buffer_empty_event = 0;
  /// NetVConnection Context.
  NetVConnectionContext_t netvc_context = NET_VCONNECTION_UNSET;

  template <typename S> void _set_service(S *instance);

private:
  void *_services[static_cast<unsigned int>(Service::N_SERVICES)] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  };

  void *_get_service(enum Service mixin_index) const;
  void _set_service(enum Service mixin_index, void *instance);
};

inline NetVConnection::NetVConnection() : VConnection(nullptr)

{
  ink_zero(local_addr);
  ink_zero(remote_addr);
}

inline void
NetVConnection::trapWriteBufferEmpty(int event)
{
  write_buffer_empty_event = event;
}

inline void *
NetVConnection::_get_service(enum NetVConnection::Service service) const
{
  return _services[static_cast<unsigned int>(service)];
}

inline void
NetVConnection::_set_service(enum NetVConnection::Service service, void *instance)
{
  this->_services[static_cast<unsigned int>(service)] = instance;
}

class ALPNSupport;
template <>
inline ALPNSupport *
NetVConnection::get_service() const
{
  return static_cast<ALPNSupport *>(this->_get_service(NetVConnection::Service::TLS_ALPN));
}
template <>
inline void
NetVConnection::_set_service(ALPNSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_ALPN, instance);
}

class TLSBasicSupport;
template <>
inline TLSBasicSupport *
NetVConnection::get_service() const
{
  return static_cast<TLSBasicSupport *>(this->_get_service(NetVConnection::Service::TLS_Basic));
}
template <>
inline void
NetVConnection::_set_service(TLSBasicSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_Basic, instance);
}

class TLSEarlyDataSupport;
template <>
inline TLSEarlyDataSupport *
NetVConnection::get_service() const
{
  return static_cast<TLSEarlyDataSupport *>(this->_get_service(NetVConnection::Service::TLS_EarlyData));
}
template <>
inline void
NetVConnection::_set_service(TLSEarlyDataSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_EarlyData, instance);
}

class TLSCertSwitchSupport;
template <>
inline TLSCertSwitchSupport *
NetVConnection::get_service() const
{
  return static_cast<TLSCertSwitchSupport *>(this->_get_service(NetVConnection::Service::TLS_CertSwitch));
}
template <>
inline void
NetVConnection::_set_service(TLSCertSwitchSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_CertSwitch, instance);
}

class TLSSNISupport;
template <>
inline TLSSNISupport *
NetVConnection::get_service() const
{
  return static_cast<TLSSNISupport *>(this->_get_service(NetVConnection::Service::TLS_SNI));
}
template <>
inline void
NetVConnection::_set_service(TLSSNISupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_SNI, instance);
}

class TLSSessionResumptionSupport;
template <>
inline TLSSessionResumptionSupport *
NetVConnection::get_service() const
{
  return static_cast<TLSSessionResumptionSupport *>(this->_get_service(NetVConnection::Service::TLS_SessionResumption));
}
template <>
inline void
NetVConnection::_set_service(TLSSessionResumptionSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_SessionResumption, instance);
}

class TLSTunnelSupport;
template <>
inline TLSTunnelSupport *
NetVConnection::get_service() const
{
  return static_cast<TLSTunnelSupport *>(this->_get_service(NetVConnection::Service::TLS_Tunnel));
}
template <>
inline void
NetVConnection::_set_service(TLSTunnelSupport *instance)
{
  this->_set_service(NetVConnection::Service::TLS_Tunnel, instance);
}

class QUICSupport;
template <>
inline QUICSupport *
NetVConnection::get_service() const
{
  return static_cast<QUICSupport *>(this->_get_service(NetVConnection::Service::QUIC));
}
template <>
inline void
NetVConnection::_set_service(QUICSupport *instance)
{
  this->_set_service(NetVConnection::Service::QUIC, instance);
}
