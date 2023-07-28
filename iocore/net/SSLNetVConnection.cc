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

#include "tscore/ink_config.h"
#include "tscore/EventNotify.h"
#include "tscore/I_Layout.h"
#include "tscore/TSSystemState.h"

#include "api/InkAPIInternal.h" // Added to include the ssl_hook definitions
#include "HttpTunnel.h"
#include "ProxyProtocol.h"
#include "HttpConfig.h"
#include "SSLSNIConfig.h"

#include "P_Net.h"
#include "P_SSLUtils.h"
#include "P_SSLNextProtocolSet.h"
#include "P_SSLConfig.h"
#include "P_SSLClientUtils.h"
#include "P_SSLNetVConnection.h"
#include "BIO_fastopen.h"
#include "SSLAPIHooks.h"
#include "SSLStats.h"
#include "P_ALPNSupport.h"

#include <netinet/in.h>

#include <string>
#include <cstring>

using namespace std::literals;

#if TS_USE_TLS_ASYNC
#include <openssl/async.h>
#endif

// This is missing from BoringSSL
#ifndef BIO_eof
#define BIO_eof(b) (int)BIO_ctrl(b, BIO_CTRL_EOF, 0, nullptr)
#endif

#define SSL_READ_ERROR_NONE        0
#define SSL_READ_ERROR             1
#define SSL_READ_READY             2
#define SSL_READ_COMPLETE          3
#define SSL_READ_WOULD_BLOCK       4
#define SSL_READ_EOS               5
#define SSL_HANDSHAKE_WANT_READ    6
#define SSL_HANDSHAKE_WANT_WRITE   7
#define SSL_HANDSHAKE_WANT_ACCEPT  8
#define SSL_HANDSHAKE_WANT_CONNECT 9
#define SSL_WRITE_WOULD_BLOCK      10
#define SSL_WAIT_FOR_HOOK          11
#define SSL_WAIT_FOR_ASYNC         12
#define SSL_RESTART                13

ClassAllocator<SSLNetVConnection> sslNetVCAllocator("sslNetVCAllocator");

namespace
{
DbgCtl dbg_ctl_ssl_early_data{"ssl_early_data"};
DbgCtl dbg_ctl_ssl_early_data_show_received{"ssl_early_data_show_received"};
DbgCtl dbg_ctl_ssl{"ssl"};
DbgCtl dbg_ctl_v_ssl{"v_ssl"};
DbgCtl dbg_ctl_ssl_error{"ssl.error"};
DbgCtl dbg_ctl_ssl_error_accept{"ssl.error.accept"};
DbgCtl dbg_ctl_ssl_error_connect{"ssl.error.connect"};
DbgCtl dbg_ctl_ssl_error_write{"ssl.error.write"};
DbgCtl dbg_ctl_ssl_error_read{"ssl.error.read"};
DbgCtl dbg_ctl_ssl_shutdown{"ssl-shutdown"};
DbgCtl dbg_ctl_ssl_alpn{"ssl_alpn"};
DbgCtl dbg_ctl_ssl_origin_session_cache{"ssl.origin_session_cache"};
DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};

/// Callback to get two locks.
/// The lock for this continuation, and for the target continuation.
class ContWrapper : public Continuation
{
public:
  /** Constructor.
      This takes the secondary @a mutex and the @a target continuation
      to invoke, along with the arguments for that invocation.
  */
  ContWrapper(ProxyMutex *mutex,             ///< Mutex for this continuation (primary lock).
              Continuation *target,          ///< "Real" continuation we want to call.
              int eventId = EVENT_IMMEDIATE, ///< Event ID for invocation of @a target.
              void *edata = nullptr          ///< Data for invocation of @a target.
              )
    : Continuation(mutex), _target(target), _eventId(eventId), _edata(edata)
  {
    SET_HANDLER(&ContWrapper::event_handler);
  }

  /// Required event handler method.
  int
  event_handler(int, void *)
  {
    EThread *eth = this_ethread();

    MUTEX_TRY_LOCK(lock, _target->mutex, eth);
    if (lock.is_locked()) { // got the target lock, we can proceed.
      _target->handleEvent(_eventId, _edata);
      delete this;
    } else { // can't get both locks, try again.
      eventProcessor.schedule_imm(this, ET_NET);
    }
    return 0;
  }

  /** Convenience static method.

      This lets a client make one call and not have to (accurately)
      copy the invocation logic embedded here. We duplicate it near
      by textually so it is easier to keep in sync.

      This takes the same arguments as the constructor but, if the
      lock can be obtained immediately, does not construct an
      instance but simply calls the @a target.
  */
  static void
  wrap(ProxyMutex *mutex,             ///< Mutex for this continuation (primary lock).
       Continuation *target,          ///< "Real" continuation we want to call.
       int eventId = EVENT_IMMEDIATE, ///< Event ID for invocation of @a target.
       void *edata = nullptr          ///< Data for invocation of @a target.
  )
  {
    EThread *eth = this_ethread();
    if (!target->mutex) {
      // If there's no mutex, plugin doesn't care about locking so why should we?
      target->handleEvent(eventId, edata);
    } else {
      MUTEX_TRY_LOCK(lock, target->mutex, eth);
      if (lock.is_locked()) {
        target->handleEvent(eventId, edata);
      } else {
        eventProcessor.schedule_imm(new ContWrapper(mutex, target, eventId, edata), ET_NET);
      }
    }
  }

private:
  Continuation *_target; ///< Continuation to invoke.
  int _eventId;          ///< with this event
  void *_edata;          ///< and this data
};
} // namespace

//
// Private
//

char const *
SSLNetVConnection::get_ssl_handshake_hook_state_name(SSLHandshakeHookState state)
{
  switch (state) {
  case HANDSHAKE_HOOKS_PRE:
    return "TS_SSL_HOOK_PRE_ACCEPT";
  case HANDSHAKE_HOOKS_PRE_INVOKE:
    return "TS_SSL_HOOK_PRE_ACCEPT_INVOKE";
  case HANDSHAKE_HOOKS_CLIENT_HELLO:
    return "TS_SSL_HOOK_CLIENT_HELLO";
  case HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    return "TS_SSL_HOOK_CLIENT_HELLO_INVOKE";
  case HANDSHAKE_HOOKS_SNI:
    return "TS_SSL_HOOK_SERVERNAME";
  case HANDSHAKE_HOOKS_CERT:
    return "TS_SSL_HOOK_CERT";
  case HANDSHAKE_HOOKS_CERT_INVOKE:
    return "TS_SSL_HOOK_CERT_INVOKE";
  case HANDSHAKE_HOOKS_CLIENT_CERT:
    return "TS_SSL_HOOK_CLIENT_CERT";
  case HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
    return "TS_SSL_HOOK_CLIENT_CERT_INVOKE";
  case HANDSHAKE_HOOKS_OUTBOUND_PRE:
    return "TS_SSL_HOOK_PRE_CONNECT";
  case HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
    return "TS_SSL_HOOK_PRE_CONNECT_INVOKE";
  case HANDSHAKE_HOOKS_VERIFY_SERVER:
    return "TS_SSL_HOOK_VERIFY_SERVER";
  case HANDSHAKE_HOOKS_DONE:
    return "TS_SSL_HOOKS_DONE";
  }
  return "unknown handshake hook name";
}

void
SSLNetVConnection::_make_ssl_connection(SSL_CTX *ctx)
{
  if (likely(this->ssl = SSL_new(ctx))) {
    // Only set up the bio stuff for the server side
    if (this->get_context() == NET_VCONNECTION_OUT) {
      BIO *bio = BIO_new(const_cast<BIO_METHOD *>(BIO_s_fastopen()));
      BIO_set_fd(bio, this->get_socket(), BIO_NOCLOSE);

      if (this->options.f_tcp_fastopen) {
        BIO_set_conn_address(bio, this->get_remote_addr());
      }

      SSL_set_bio(ssl, bio, bio);
    } else {
      this->initialize_handshake_buffers();
      BIO *rbio = BIO_new(BIO_s_mem());
      BIO *wbio = BIO_new_socket(this->get_socket(), BIO_NOCLOSE);
      BIO_set_mem_eof_return(wbio, -1);
      SSL_set_bio(ssl, rbio, wbio);

#if TS_HAS_TLS_EARLY_DATA
      update_early_data_config(SSLConfigParams::server_max_early_data, SSLConfigParams::server_recv_max_early_data);
#endif
    }
    this->_bindSSLObject();
  }
}

void
SSLNetVConnection::_bindSSLObject()
{
  SSLNetVCAttach(this->ssl, this);
  TLSBasicSupport::bind(this->ssl, this);
  ALPNSupport::bind(this->ssl, this);
  TLSSessionResumptionSupport::bind(this->ssl, this);
  TLSSNISupport::bind(this->ssl, this);
  TLSEarlyDataSupport::bind(this->ssl, this);
  TLSTunnelSupport::bind(this->ssl, this);
  TLSCertSwitchSupport::bind(this->ssl, this);
}

void
SSLNetVConnection::_unbindSSLObject()
{
  SSLNetVCDetach(this->ssl);
  TLSBasicSupport::unbind(this->ssl);
  ALPNSupport::unbind(this->ssl);
  TLSSessionResumptionSupport::unbind(this->ssl);
  TLSSNISupport::unbind(this->ssl);
  TLSEarlyDataSupport::unbind(this->ssl);
  TLSTunnelSupport::unbind(this->ssl);
  TLSCertSwitchSupport::unbind(this->ssl);
}

static void
debug_certificate_name(const char *msg, X509_NAME *name)
{
  BIO *bio;

  if (name == nullptr) {
    return;
  }

  bio = BIO_new(BIO_s_mem());
  if (bio == nullptr) {
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    long len;
    char *ptr;
    len = BIO_get_mem_data(bio, &ptr);
    Dbg(dbg_ctl_ssl, "%s %.*s", msg, (int)len, ptr);
  }

  BIO_free(bio);
}

int
SSLNetVConnection::_ssl_read_from_net(EThread *lthread, int64_t &ret)
{
  NetState *s            = &this->read;
  MIOBufferAccessor &buf = s->vio.buffer;
  int event              = SSL_READ_ERROR_NONE;
  int64_t bytes_read     = 0;
  ssl_error_t sslErr     = SSL_ERROR_NONE;

  int64_t toread = buf.writer()->write_avail();
  ink_release_assert(toread > 0);
  if (toread > s->vio.ntodo()) {
    toread = s->vio.ntodo();
  }

  bytes_read = 0;
  while (sslErr == SSL_ERROR_NONE && bytes_read < toread) {
    int64_t nread             = 0;
    int64_t block_write_avail = buf.writer()->block_write_avail();
    ink_release_assert(block_write_avail > 0);
    int64_t amount_to_read = toread - bytes_read;
    if (amount_to_read > block_write_avail) {
      amount_to_read = block_write_avail;
    }

    Dbg(dbg_ctl_ssl, "amount_to_read=%" PRId64, amount_to_read);
    char *current_block = buf.writer()->end();
    ink_release_assert(current_block != nullptr);
    sslErr = this->_ssl_read_buffer(current_block, amount_to_read, nread);

    Dbg(dbg_ctl_ssl, "nread=%" PRId64, nread);

    switch (sslErr) {
    case SSL_ERROR_NONE:
#if DEBUG
    {
      static DbgCtl dbg_ctl{"ssl_buff"};
      SSLDebugBufferPrint(dbg_ctl, current_block, nread, "SSL Read");
    }
#endif
      ink_assert(nread);
      bytes_read += nread;
      if (nread > 0) {
        buf.writer()->fill(nread); // Tell the buffer, we've used the bytes
        this->netActivity(lthread);
      }
      break;
    case SSL_ERROR_WANT_WRITE:
      event = SSL_WRITE_WOULD_BLOCK;
      Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WOULD_BLOCK(write)");
      break;
    case SSL_ERROR_WANT_READ:
      event = SSL_READ_WOULD_BLOCK;
      Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WOULD_BLOCK(read)");
      break;
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
    case SSL_ERROR_WANT_CLIENT_HELLO_CB:
      event = SSL_READ_WOULD_BLOCK;
      Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WOULD_BLOCK(read/client hello cb)");
      break;
#endif
    case SSL_ERROR_WANT_X509_LOOKUP:
      event = SSL_READ_WOULD_BLOCK;
      Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WOULD_BLOCK(read/x509 lookup)");
      break;
    case SSL_ERROR_SYSCALL:
      if (nread != 0) {
        // not EOF
        Metrics::increment(ssl_rsb.error_syscall);
        event = SSL_READ_ERROR;
        ret   = errno;
        Dbg(dbg_ctl_ssl_error, "SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
      } else {
        // then EOF observed, treat it as EOS
        event = SSL_READ_EOS;
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      event = SSL_READ_EOS;
      Dbg(dbg_ctl_ssl_error, "SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      event = SSL_READ_ERROR;
      ret   = errno;
      SSLVCDebug(this, "errno=%d", errno);
      Metrics::increment(ssl_rsb.error_ssl);
    } break;
    } // switch
  }   // while

  if (bytes_read > 0) {
    Dbg(dbg_ctl_ssl, "bytes_read=%" PRId64, bytes_read);

    s->vio.ndone += bytes_read;
    this->netActivity(lthread);

    ret = bytes_read;

    // If we read it all, don't worry about the other events and just send read complete
    event = (s->vio.ntodo() <= 0) ? SSL_READ_COMPLETE : SSL_READ_READY;
    if (sslErr == SSL_ERROR_NONE && s->vio.ntodo() > 0) {
      // We stopped with data on the wire (to avoid overbuffering).  Make sure we are triggered
      this->read.triggered = 1;
    }
  } else { // if( bytes_read > 0 )
#if defined(_DEBUG)
    if (bytes_read == 0) {
      Dbg(dbg_ctl_ssl, "bytes_read == 0");
    }
#endif
  }
  return event;
}

/** Read from socket directly for handshake data.  Store the data in an MIOBuffer.  Place the data in
 * the read BIO so the openssl library has access to it. If for some reason we must abort out of the
 * handshake, the stored data can be replayed (e.g. back out to blind tunneling)
 */
int64_t
SSLNetVConnection::read_raw_data()
{
  // read data
  int64_t r          = 0;
  int64_t total_read = 0;
  int64_t rattempted = 0;
  char *buffer       = nullptr;
  int buf_len;
  IOBufferBlock *b = this->handShakeBuffer->first_write_block();

  rattempted = b->write_avail();
  while (rattempted) {
    buffer  = b->_end;
    buf_len = rattempted;
    b       = b->next.get();

    r = SocketManager::read(this->con.fd, buffer, buf_len);
    Metrics::increment(net_rsb.calls_to_read);
    total_read += rattempted;

    Dbg(dbg_ctl_ssl, "read_raw_data r=%" PRId64 " rattempted=%" PRId64 " total_read=%" PRId64 " fd=%d", r, rattempted, total_read,
        con.fd);
    // last read failed or was incomplete
    if (r != rattempted || !b) {
      break;
    }

    rattempted = b->write_avail();
  }
  // If we have already moved some bytes successfully, adjust total_read to reflect reality
  // If any read succeeded, we should return success
  if (r != rattempted) {
    // If the first read fails, we should return error
    if (r <= 0 && total_read > rattempted) {
      r = total_read - rattempted;
    } else {
      r = total_read - rattempted + r;
    }
  }
  Metrics::increment(net_rsb.read_bytes, r);
  Metrics::increment(net_rsb.read_bytes_count);

  swoc::IPRangeSet *pp_ipmap;
  pp_ipmap = SSLConfigParams::proxy_protocol_ip_addrs;

  if (this->get_is_proxy_protocol() && this->get_proxy_protocol_version() == ProxyProtocolVersion::UNDEFINED) {
    Dbg(dbg_ctl_proxyprotocol, "proxy protocol is enabled on this port");
    if (pp_ipmap->count() > 0) {
      Dbg(dbg_ctl_proxyprotocol, "proxy protocol has a configured allowlist of trusted IPs - checking");

      // At this point, using get_remote_addr() will return the ip of the
      // proxy source IP, not the Proxy Protocol client ip. Since we are
      // checking the ip of the actual source of this connection, this is
      // what we want now.
      if (!pp_ipmap->contains(swoc::IPAddr(get_remote_addr()))) {
        Dbg(dbg_ctl_proxyprotocol, "Source IP is NOT in the configured allowlist of trusted IPs - closing connection");
        r = -ENOTCONN; // Need a quick close/exit here to refuse the connection!!!!!!!!!
        goto proxy_protocol_bypass;
      } else {
        char new_host[INET6_ADDRSTRLEN];
        Dbg(dbg_ctl_proxyprotocol, "Source IP [%s] is in the trusted allowlist for proxy protocol",
            ats_ip_ntop(this->get_remote_addr(), new_host, sizeof(new_host)));
      }
    } else {
      Dbg(dbg_ctl_proxyprotocol, "proxy protocol DOES NOT have a configured allowlist of trusted IPs but "
                                 "proxy protocol is enabled on this port - processing all connections");
    }

    auto const stored_r = r;
    if (this->has_proxy_protocol(buffer, &r)) {
      Dbg(dbg_ctl_proxyprotocol, "ssl has proxy protocol header");
      if (dbg_ctl_proxyprotocol.on()) {
        IpEndpoint dst;
        dst.sa = *(this->get_proxy_protocol_dst_addr());
        ip_port_text_buffer ipb1;
        ats_ip_nptop(&dst, ipb1, sizeof(ipb1));
        DbgPrint(dbg_ctl_proxyprotocol, "ssl_has_proxy_v1, dest IP received [%s]", ipb1);
      }
    } else {
      Dbg(dbg_ctl_proxyprotocol, "proxy protocol was enabled, but Proxy Protocol header was not present");
      // We are flexible with the Proxy Protocol designation. Maybe not all
      // connections include Proxy Protocol. Revert to the stored value of r so
      // we can process the bytes that are on the wire (likely a CLIENT_HELLO).
      r = stored_r;
    }
  } // end of Proxy Protocol processing

proxy_protocol_bypass:

  if (r > 0) {
    this->handShakeBuffer->fill(r);

    char *start              = this->handShakeReader->start();
    char *end                = this->handShakeReader->end();
    this->handShakeBioStored = end - start;

    // Sets up the buffer as a read only bio target
    // Must be reset on each read
    BIO *rbio = BIO_new_mem_buf(start, this->handShakeBioStored);
    BIO_set_mem_eof_return(rbio, -1);
    SSL_set0_rbio(this->ssl, rbio);
  } else {
    this->handShakeBioStored = 0;
  }

  Dbg(dbg_ctl_ssl, "%p read r=%" PRId64 " total=%" PRId64 " bio=%d\n", this, r, total_read, this->handShakeBioStored);

  // check for errors
  if (r <= 0) {
    if (r == -EAGAIN || r == -ENOTCONN) {
      Metrics::increment(net_rsb.calls_to_read_nodata);
    }
  }

  return r;
}

//
// Return true if we updated the rbio with another
// memory chunk (should be ready for another read right away)
//
bool
SSLNetVConnection::update_rbio(bool move_to_socket)
{
  bool retval = false;
  if (BIO_eof(SSL_get_rbio(this->ssl)) && this->handShakeReader != nullptr) {
    this->handShakeReader->consume(this->handShakeBioStored);
    this->handShakeBioStored = 0;
    // Load up the next block if present
    if (this->handShakeReader->is_read_avail_more_than(0)) {
      // Setup the next iobuffer block to drain
      char *start              = this->handShakeReader->start();
      char *end                = this->handShakeReader->end();
      this->handShakeBioStored = end - start;

      // Sets up the buffer as a read only bio target
      // Must be reset on each read
      BIO *rbio = BIO_new_mem_buf(start, this->handShakeBioStored);
      BIO_set_mem_eof_return(rbio, -1);
      SSL_set0_rbio(this->ssl, rbio);
      retval = true;
      // Handshake buffer is empty but we have read something, move to the socket rbio
    } else if (move_to_socket && this->handShakeHolder->is_read_avail_more_than(0)) {
      BIO *rbio = BIO_new_socket(this->get_socket(), BIO_NOCLOSE);
      BIO_set_mem_eof_return(rbio, -1);
      SSL_set0_rbio(this->ssl, rbio);
      free_handshake_buffers();
    }
  }
  return retval;
}

// changed by YTS Team, yamsat
void
SSLNetVConnection::net_read_io(NetHandler *nh, EThread *lthread)
{
  int ret;
  int64_t r     = 0;
  int64_t bytes = 0;
  NetState *s   = &this->read;

  if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == this->attributes) {
    this->super::net_read_io(nh, lthread);
    return;
  }

  MUTEX_TRY_LOCK(lock, s->vio.mutex, lthread);
  if (!lock.is_locked()) {
    readReschedule(nh);
    return;
  }
  // Got closed by the HttpSessionManager thread during a migration
  // The closed flag should be stable once we get the s->vio.mutex in that case
  // (the global session pool mutex).
  if (this->closed) {
    this->super::net_read_io(nh, lthread);
    return;
  }
  // If the key renegotiation failed it's over, just signal the error and finish.
  if (sslClientRenegotiationAbort == true) {
    this->read.triggered = 0;
    readSignalError(nh, -ENET_SSL_FAILED);
    Dbg(dbg_ctl_ssl, "client renegotiation setting read signal error");
    return;
  }

  // If it is not enabled, lower its priority.  This allows
  // a fast connection to speed match a slower connection by
  // shifting down in priority even if it could read.
  if (!s->enabled || s->vio.op != VIO::READ || s->vio.is_disabled()) {
    read_disable(nh, this);
    return;
  }

  MIOBufferAccessor &buf = s->vio.buffer;
  int64_t ntodo          = s->vio.ntodo();
  ink_assert(buf.writer());

  // Continue on if we are still in the handshake
  if (!getSSLHandShakeComplete()) {
    int err = 0;

    if (get_context() == NET_VCONNECTION_OUT) {
      ret = sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = sslStartHandShake(SSL_EVENT_SERVER, err);
    }
    if (ret == SSL_RESTART) {
      // VC migrated into a new object
      // Just give up and go home. Events should trigger on the new vc
      Dbg(dbg_ctl_ssl, "Restart for allow plain");
      return;
    }
    // If we have flipped to blind tunnel, don't read ahead
    if (this->handShakeReader) {
      if (this->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
        // Now in blind tunnel. Set things up to read what is in the buffer
        // Must send the READ_COMPLETE here before considering
        // forwarding on the handshake buffer, so the
        // SSLNextProtocolTrampoline has a chance to do its
        // thing before forwarding the buffers.
        this->readSignalDone(VC_EVENT_READ_COMPLETE, nh);

        // If the handshake isn't set yet, this means the tunnel
        // decision was make in the SNI callback.  We must move
        // the client hello message back into the standard read.vio
        // so it will get forwarded onto the origin server
        if (!this->getSSLHandShakeComplete()) {
          this->sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;

          // Copy over all data already read in during the SSL_accept
          // (the client hello message)
          NetState *s             = &this->read;
          MIOBufferAccessor &buf  = s->vio.buffer;
          int64_t r               = buf.writer()->write(this->handShakeHolder);
          s->vio.nbytes          += r;
          s->vio.ndone           += r;

          // Clean up the handshake buffers
          this->free_handshake_buffers();

          if (r > 0) {
            // Kick things again, so the data that was copied into the
            // vio.read buffer gets processed
            this->readSignalDone(VC_EVENT_READ_COMPLETE, nh);
          }
        }
        return; // Leave if we are tunneling
      }
    }
    if (ret == EVENT_ERROR) {
      this->read.triggered = 0;
      readSignalError(nh, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT) {
      if (SSLConfigParams::ssl_handshake_timeout_in > 0) {
        double handshake_time = (static_cast<double>(ink_get_hrtime() - this->get_tls_handshake_begin_time()) / 1000000000);
        Dbg(dbg_ctl_ssl, "ssl handshake for vc %p, took %.3f seconds, configured handshake_timer: %d", this, handshake_time,
            SSLConfigParams::ssl_handshake_timeout_in);
        if (handshake_time > SSLConfigParams::ssl_handshake_timeout_in) {
          Dbg(dbg_ctl_ssl, "ssl handshake for vc %p, expired, release the connection", this);
          read.triggered = 0;
          nh->read_ready_list.remove(this);
          readSignalError(nh, ETIMEDOUT);
          return;
        }
      }
      // move over to the socket if we haven't already
      if (this->handShakeBuffer != nullptr) {
        read.triggered = update_rbio(true);
      } else {
        read.triggered = 0;
      }
      if (!read.triggered) {
        nh->read_ready_list.remove(this);
      }
      readReschedule(nh);
    } else if (ret == SSL_HANDSHAKE_WANT_CONNECT || ret == SSL_HANDSHAKE_WANT_WRITE) {
      write.triggered = 0;
      nh->write_ready_list.remove(this);
      writeReschedule(nh);
    } else if (ret == EVENT_DONE) {
      Dbg(dbg_ctl_ssl, "ssl handshake EVENT_DONE ntodo=%" PRId64, ntodo);
      // If this was driven by a zero length read, signal complete when
      // the handshake is complete. Otherwise set up for continuing read
      // operations.
      if (ntodo <= 0) {
        readSignalDone(VC_EVENT_READ_COMPLETE, nh);
      } else {
        read.triggered = 1;
        if (read.enabled) {
          nh->read_ready_list.in_or_enqueue(this);
        }
      }
    } else if (ret == SSL_WAIT_FOR_HOOK || ret == SSL_WAIT_FOR_ASYNC) {
      // avoid readReschedule - done when the plugin calls us back to reenable
    } else {
      readReschedule(nh);
    }
    return;
  }

  // If there is nothing to do or no space available, disable connection
  if (ntodo <= 0 || !buf.writer()->write_avail() || s->vio.is_disabled()) {
    read_disable(nh, this);
    return;
  }

  // At this point we are at the post-handshake SSL processing
  //
  // not sure if this do-while loop is really needed here, please replace
  // this comment if you know
  int ssl_read_errno = 0;
  do {
    ret = this->_ssl_read_from_net(lthread, r);
    if (ret == SSL_READ_READY || ret == SSL_READ_ERROR_NONE) {
      bytes += r;
    }
    ink_assert(bytes >= 0);
  } while ((ret == SSL_READ_READY && bytes == 0) || ret == SSL_READ_ERROR_NONE);
  ssl_read_errno = errno;

  if (bytes > 0) {
    if (ret == SSL_READ_WOULD_BLOCK || ret == SSL_READ_READY) {
      if (readSignalAndUpdate(VC_EVENT_READ_READY) != EVENT_CONT) {
        Dbg(dbg_ctl_ssl, "readSignal != EVENT_CONT");
        return;
      }
    }
  }

  switch (ret) {
  case SSL_READ_READY:
    readReschedule(nh);
    return;
    break;
  case SSL_WRITE_WOULD_BLOCK:
  case SSL_READ_WOULD_BLOCK:
    if (lock.get_mutex() != s->vio.mutex.get()) {
      Dbg(dbg_ctl_ssl, "mutex switched");
      if (ret == SSL_READ_WOULD_BLOCK) {
        readReschedule(nh);
      } else {
        writeReschedule(nh);
      }
      return;
    }
    // reset the trigger and remove from the ready queue
    // we will need to be retriggered to read from this socket again
    read.triggered = 0;
    nh->read_ready_list.remove(this);
    Dbg(dbg_ctl_ssl, "read finished - would block");
    break;

  case SSL_READ_EOS:
    // close the connection if we have SSL_READ_EOS, this is the return value from ssl_read_from_net() if we get an
    // SSL_ERROR_ZERO_RETURN from SSL_get_error()
    // SSL_ERROR_ZERO_RETURN means that the origin server closed the SSL connection
    read.triggered = 0;
    readSignalDone(VC_EVENT_EOS, nh);

    if (bytes > 0) {
      Dbg(dbg_ctl_ssl, "read finished - EOS");
    } else {
      Dbg(dbg_ctl_ssl, "read finished - 0 useful bytes read, bytes used by SSL layer");
    }
    break;
  case SSL_READ_COMPLETE:
    readSignalDone(VC_EVENT_READ_COMPLETE, nh);
    Dbg(dbg_ctl_ssl, "read finished - signal done");
    break;
  case SSL_READ_ERROR:
    this->read.triggered = 0;
    readSignalError(nh, (ssl_read_errno) ? ssl_read_errno : -ENET_SSL_FAILED);
    Dbg(dbg_ctl_ssl, "read finished - read error");
    break;
  }
}

int64_t
SSLNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  int64_t try_to_write;
  int64_t num_really_written       = 0;
  int64_t l                        = 0;
  uint32_t dynamic_tls_record_size = 0;
  ssl_error_t err                  = SSL_ERROR_NONE;

  // Dynamic TLS record sizing
  ink_hrtime now = 0;
  if (SSLConfigParams::ssl_maxrecord == -1) {
    now                       = ink_get_hrtime();
    int msec_since_last_write = ink_hrtime_diff_msec(now, sslLastWriteTime);

    if (msec_since_last_write > SSL_DEF_TLS_RECORD_MSEC_THRESHOLD) {
      // reset sslTotalBytesSent upon inactivity for SSL_DEF_TLS_RECORD_MSEC_THRESHOLD
      sslTotalBytesSent = 0;
    }
    Dbg(dbg_ctl_ssl, "now=%" PRId64 " lastwrite=%" PRId64 " msec_since_last_write=%d", now, sslLastWriteTime,
        msec_since_last_write);
  }

  if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == this->attributes) {
    return this->super::load_buffer_and_write(towrite, buf, total_written, needs);
  }

  Dbg(dbg_ctl_ssl, "towrite=%" PRId64, towrite);

  do {
    // What is remaining left in the next block?
    l                   = buf.reader()->block_read_avail();
    char *current_block = buf.reader()->start();

    // check if to amount to write exceeds that in this buffer
    int64_t wavail = towrite - total_written;

    if (l > wavail) {
      l = wavail;
    }

    // TS-2365: If the SSL max record size is set and we have
    // more data than that, break this into smaller write
    // operations.
    //
    // TS-4424: Don't mess with record size if last SSL_write failed with
    // needs write
    if (redoWriteSize) {
      l             = redoWriteSize;
      redoWriteSize = 0;
    } else {
      if (SSLConfigParams::ssl_maxrecord > 0 && l > SSLConfigParams::ssl_maxrecord) {
        l = SSLConfigParams::ssl_maxrecord;
      } else if (SSLConfigParams::ssl_maxrecord == -1) {
        if (sslTotalBytesSent < SSL_DEF_TLS_RECORD_BYTE_THRESHOLD) {
          dynamic_tls_record_size = SSL_DEF_TLS_RECORD_SIZE;
          Metrics::increment(ssl_rsb.total_dyn_def_tls_record_count);
        } else {
          dynamic_tls_record_size = SSL_MAX_TLS_RECORD_SIZE;
          Metrics::increment(ssl_rsb.total_dyn_max_tls_record_count);
        }
        if (l > dynamic_tls_record_size) {
          l = dynamic_tls_record_size;
        }
      }
    }

    if (!l) {
      break;
    }

    try_to_write       = l;
    num_really_written = 0;
    Dbg(dbg_ctl_v_ssl, "b=%p l=%" PRId64, current_block, l);
    err = this->_ssl_write_buffer(current_block, l, num_really_written);

    // We wrote all that we thought we should
    if (num_really_written > 0) {
      total_written += num_really_written;
      buf.reader()->consume(num_really_written);
    }

    Dbg(dbg_ctl_ssl, "try_to_write=%" PRId64 " written=%" PRId64 " total_written=%" PRId64, try_to_write, num_really_written,
        total_written);
    Metrics::increment(net_rsb.calls_to_write);
  } while (num_really_written == try_to_write && total_written < towrite);

  if (total_written > 0) {
    sslLastWriteTime   = now;
    sslTotalBytesSent += total_written;
  }
  redoWriteSize = 0;
  if (num_really_written > 0) {
    needs |= EVENTIO_WRITE;
  } else {
    switch (err) {
    case SSL_ERROR_NONE:
      Dbg(dbg_ctl_ssl, "SSL_write-SSL_ERROR_NONE");
      break;
    case SSL_ERROR_WANT_READ:
      needs              |= EVENTIO_READ;
      num_really_written  = -EAGAIN;
      Dbg(dbg_ctl_ssl_error, "SSL_write-SSL_ERROR_WANT_READ");
      break;
    case SSL_ERROR_WANT_WRITE:
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
    case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
    case SSL_ERROR_WANT_X509_LOOKUP: {
      if (SSL_ERROR_WANT_WRITE == err) {
        redoWriteSize = l;
      }
      needs              |= EVENTIO_WRITE;
      num_really_written  = -EAGAIN;
      Dbg(dbg_ctl_ssl_error, "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    }
    case SSL_ERROR_SYSCALL:
      // SSL_ERROR_SYSCALL is an IO error. errno is likely 0, so set EPIPE, as
      // we do with SSL_ERROR_SSL below, to indicate a connection error.
      num_really_written = -EPIPE;
      Metrics::increment(ssl_rsb.error_syscall);
      Dbg(dbg_ctl_ssl_error, "SSL_write-SSL_ERROR_SYSCALL");
      break;
    // end of stream
    case SSL_ERROR_ZERO_RETURN:
      num_really_written = -errno;
      Dbg(dbg_ctl_ssl_error, "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      // Treat SSL_ERROR_SSL as EPIPE error.
      num_really_written = -EPIPE;
      SSLVCDebug(this, "SSL_write-SSL_ERROR_SSL errno=%d", errno);
      Metrics::increment(ssl_rsb.error_ssl);
    } break;
    }
  }
  return num_really_written;
}

SSLNetVConnection::SSLNetVConnection()
{
  this->_set_service(static_cast<ALPNSupport *>(this));
  this->_set_service(static_cast<TLSBasicSupport *>(this));
  this->_set_service(static_cast<TLSCertSwitchSupport *>(this));
  this->_set_service(static_cast<TLSEarlyDataSupport *>(this));
  this->_set_service(static_cast<TLSSNISupport *>(this));
  this->_set_service(static_cast<TLSSessionResumptionSupport *>(this));
  this->_set_service(static_cast<TLSTunnelSupport *>(this));
}

void
SSLNetVConnection::do_io_close(int lerrno)
{
  if (this->ssl != nullptr) {
    if (get_context() == NET_VCONNECTION_OUT) {
      callHooks(TS_EVENT_VCONN_OUTBOUND_CLOSE);
    } else {
      callHooks(TS_EVENT_VCONN_CLOSE);
    }

    if (getSSLHandShakeComplete()) {
      int shutdown_mode = SSL_get_shutdown(ssl);
      Dbg(dbg_ctl_ssl_shutdown, "previous shutdown state 0x%x", shutdown_mode);
      int new_shutdown_mode = shutdown_mode | SSL_RECEIVED_SHUTDOWN;

      if (new_shutdown_mode != shutdown_mode) {
        // We do not need to sit around and wait for the client's close-notify if
        // they have not already sent it.  We will still be standards compliant
        Dbg(dbg_ctl_ssl_shutdown, "new SSL_set_shutdown 0x%x", new_shutdown_mode);
        SSL_set_shutdown(ssl, new_shutdown_mode);
      }

      // If the peer has already sent a FIN, don't bother with the shutdown
      // They will just send us a RST for our troubles
      // This test is not foolproof.  The client's fin could be on the wire
      // at the same time we send the close-notify.  If so, the client will likely
      // send RST anyway
      char c;
      ssize_t x = recv(this->con.fd, &c, 1, MSG_PEEK);
      // x < 0 means error.  x == 0 means fin sent
      bool do_shutdown = (x > 0);
      if (x < 0) {
        do_shutdown = (errno == EAGAIN || errno == EWOULDBLOCK);
      }
      if (do_shutdown) {
        // Send the close-notify
        int ret = SSL_shutdown(ssl);
        Dbg(dbg_ctl_ssl_shutdown, "SSL_shutdown %s", (ret) ? "success" : "failed");
      } else {
        // Request a quiet shutdown to OpenSSL
        SSL_set_quiet_shutdown(ssl, 1);
        SSL_set_shutdown(ssl, SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN);
        Dbg(dbg_ctl_ssl_shutdown, "Enable quiet shutdown");
      }
    }
  }
  // Go on and do the unix socket cleanups
  super::do_io_close(lerrno);
}

void
SSLNetVConnection::clear()
{
  _ca_cert_file.reset();
  _ca_cert_dir.reset();

  // SSL_SESSION_free() must only be called for SSL_SESSION objects,
  // for which the reference count was explicitly incremented (e.g.
  // by calling SSL_get1_session(), see SSL_get_session(3)) or when
  // the SSL_SESSION object was generated outside a TLS handshake
  // operation, e.g. by using d2i_SSL_SESSION(3). It must not be called
  // on other SSL_SESSION objects, as this would cause incorrect
  // reference counts and therefore program failures.
  // Since we created the shared pointer with a custom deleter,
  // resetting here will decrement the ref-counter.
  client_sess.reset();

  if (ssl != nullptr) {
    SSL_free(ssl);
    ssl = nullptr;
  }

  ALPNSupport::clear();
  TLSBasicSupport::clear();
  TLSSessionResumptionSupport::clear();
  TLSSNISupport::_clear();
  TLSTunnelSupport::_clear();
  TLSCertSwitchSupport::_clear();

  sslHandshakeStatus          = SSLHandshakeStatus::SSL_HANDSHAKE_ONGOING;
  sslLastWriteTime            = 0;
  sslTotalBytesSent           = 0;
  sslClientRenegotiationAbort = false;

  curHook         = nullptr;
  hookOpRequested = SSL_HOOK_OP_DEFAULT;
  free_handshake_buffers();

  super::clear();
}
void
SSLNetVConnection::free_thread(EThread *t)
{
  ink_release_assert(t == this_ethread());

  // close socket fd
  if (con.fd != NO_FD) {
    Metrics::decrement(net_rsb.connections_currently_open);
  }
  con.close();

#if TS_HAS_TLS_EARLY_DATA
  if (_early_data_reader != nullptr) {
    _early_data_reader->dealloc();
  }

  if (_early_data_buf != nullptr) {
    free_MIOBuffer(_early_data_buf);
  }

  _early_data_reader = nullptr;
  _early_data_buf    = nullptr;
#endif

  clear();
  SET_CONTINUATION_HANDLER(this, &SSLNetVConnection::startEvent);
  ink_assert(con.fd == NO_FD);
  ink_assert(t == this_ethread());

  if (from_accept_thread) {
    sslNetVCAllocator.free(this);
  } else {
    ink_assert(con.fd == NO_FD);
    THREAD_FREE(this, sslNetVCAllocator, t);
  }
}

int
SSLNetVConnection::sslStartHandShake(int event, int &err)
{
  if (TSSystemState::is_ssl_handshaking_stopped()) {
    Dbg(dbg_ctl_ssl, "Stopping handshake due to server shutting down.");
    return EVENT_ERROR;
  }
  if (this->get_tls_handshake_begin_time() == 0) {
    this->_record_tls_handshake_begin_time();
    // net_activity will not be triggered until after the handshake
    set_inactivity_timeout(HRTIME_SECONDS(SSLConfigParams::ssl_handshake_timeout_in));
  }
  SSLConfig::scoped_config params;
  switch (event) {
  case SSL_EVENT_SERVER:
    if (this->ssl == nullptr) {
      SSLCertificateConfig::scoped_config lookup;
      IpEndpoint dst;
      int namelen = sizeof(dst);
      if (0 != safe_getsockname(this->get_socket(), &dst.sa, &namelen)) {
        Dbg(dbg_ctl_ssl, "Failed to get dest ip, errno = [%d]", errno);
        return EVENT_ERROR;
      }
      SSLCertContext *cc = lookup->find(dst);
      if (dbg_ctl_ssl.on()) {
        IpEndpoint src;
        ip_port_text_buffer ipb1, ipb2;
        int ip_len = sizeof(src);

        if (0 != safe_getpeername(this->get_socket(), &src.sa, &ip_len)) {
          DbgPrint(dbg_ctl_ssl, "Failed to get src ip, errno = [%d]", errno);
          return EVENT_ERROR;
        }
        ats_ip_nptop(&dst, ipb1, sizeof(ipb1));
        ats_ip_nptop(&src, ipb2, sizeof(ipb2));
        DbgPrint(dbg_ctl_ssl, "IP context is %p for [%s] -> [%s], default context %p", cc, ipb2, ipb1, lookup->defaultContext());
      }

      // Escape if this is marked to be a tunnel.
      // No data has been read at this point, so we can go
      // directly into blind tunnel mode

      if (cc && SSLCertContextOption::OPT_TUNNEL == cc->opt) {
        if (this->is_transparent) {
          this->attributes   = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
          sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;
          SSL_free(this->ssl);
          this->ssl = nullptr;
          return EVENT_DONE;
        } else {
          hookOpRequested = SSL_HOOK_OP_TUNNEL;
        }
      }

      // Attach the default SSL_CTX to this SSL session. The default context is never going to be able
      // to negotiate a SSL session, but it's enough to trampoline us into the SNI callback where we
      // can select the right server certificate.
      this->_make_ssl_connection(lookup->defaultContext());
    }

    if (this->ssl == nullptr) {
      SSLErrorVC(this, "failed to create SSL server session");
      return EVENT_ERROR;
    }
    return sslServerHandShakeEvent(err);

  case SSL_EVENT_CLIENT:

    char buff[INET6_ADDRSTRLEN];

    if (this->ssl == nullptr) {
      // Making the check here instead of later, so we only
      // do this setting immediately after we create the SSL object
      SNIConfig::scoped_config sniParam;
      const char *serverKey = this->options.sni_servername;
      if (!serverKey) {
        ats_ip_ntop(this->get_remote_addr(), buff, INET6_ADDRSTRLEN);
        serverKey = buff;
      }
      auto nps                 = sniParam->get_property_config(serverKey);
      shared_SSL_CTX sharedCTX = nullptr;
      SSL_CTX *clientCTX       = nullptr;

      // First Look to see if there are override parameters
      Dbg(dbg_ctl_ssl, "Checking for outbound client cert override [%p]", options.ssl_client_cert_name.get());
      if (options.ssl_client_cert_name) {
        std::string certFilePath;
        std::string keyFilePath;
        std::string caCertFilePath;
        // Enable override to explicitly disable the client certificate. That is, don't fill
        // in any of the cert paths if the cert file name is empty or "NULL".
        if (*options.ssl_client_cert_name != '\0' && 0 != strcasecmp("NULL", options.ssl_client_cert_name)) {
          certFilePath = Layout::get()->relative_to(params->clientCertPathOnly, options.ssl_client_cert_name.get());
          if (options.ssl_client_private_key_name) {
            keyFilePath = Layout::get()->relative_to(params->clientKeyPathOnly, options.ssl_client_private_key_name);
          }
          if (options.ssl_client_ca_cert_name) {
            caCertFilePath = Layout::get()->relative_to(params->clientCACertPath, options.ssl_client_ca_cert_name);
          }
          Dbg(dbg_ctl_ssl, "Using outbound client cert `%s'", options.ssl_client_cert_name.get());
        } else {
          Dbg(dbg_ctl_ssl, "Clearing outbound client cert");
        }
        sharedCTX =
          params->getCTX(certFilePath, keyFilePath, caCertFilePath.empty() ? params->clientCACertFilename : caCertFilePath.c_str(),
                         params->clientCACertPath);
      } else if (options.ssl_client_ca_cert_name) {
        std::string caCertFilePath = Layout::get()->relative_to(params->clientCACertPath, options.ssl_client_ca_cert_name);
        sharedCTX = params->getCTX(params->clientCertPath, params->clientKeyPath, caCertFilePath.c_str(), params->clientCACertPath);
      } else if (nps && !nps->client_cert_file.empty()) {
        // If no overrides available, try the available nextHopProperty by reading from context mappings
        sharedCTX =
          params->getCTX(nps->client_cert_file, nps->client_key_file, params->clientCACertFilename, params->clientCACertPath);
      } else { // Just stay with the values passed down from the SM for verify
        clientCTX = params->client_ctx.get();
      }

      if (sharedCTX) {
        clientCTX = sharedCTX.get();
      }

      if (options.verifyServerPolicy != YamlSNIConfig::Policy::UNSET) {
        // Stay with conf-override version as the highest priority
      } else if (nps && nps->verify_server_policy != YamlSNIConfig::Policy::UNSET) {
        options.verifyServerPolicy = nps->verify_server_policy;
      } else {
        options.verifyServerPolicy = params->verifyServerPolicy;
      }

      if (options.verifyServerProperties != YamlSNIConfig::Property::UNSET) {
        // Stay with conf-override version as the highest priority
      } else if (nps && nps->verify_server_properties != YamlSNIConfig::Property::UNSET) {
        options.verifyServerProperties = nps->verify_server_properties;
      } else {
        options.verifyServerProperties = params->verifyServerProperties;
      }

      if (!clientCTX) {
        SSLErrorVC(this, "failed to create SSL client session");
        return EVENT_ERROR;
      }

      this->_make_ssl_connection(clientCTX);
      if (this->ssl == nullptr) {
        SSLErrorVC(this, "failed to create SSL client session");
        return EVENT_ERROR;
      }

      // If it is negative, we are consciously not setting ALPN (e.g. for private server sessions)
      if (options.alpn_protocols_array_size >= 0) {
        if (options.alpn_protocols_array_size > 0) {
          SSL_set_alpn_protos(this->ssl, options.alpn_protocols_array, options.alpn_protocols_array_size);
        } else if (params->alpn_protocols_array_size > 0) {
          // Set the ALPN protocols we are requesting.
          SSL_set_alpn_protos(this->ssl, params->alpn_protocols_array, params->alpn_protocols_array_size);
        }
      }

      SSL_set_verify(this->ssl, SSL_VERIFY_PEER, verify_callback);

      // SNI
      ats_scoped_str &tlsext_host_name = this->options.sni_hostname ? this->options.sni_hostname : this->options.sni_servername;
      if (tlsext_host_name) {
        if (SSL_set_tlsext_host_name(this->ssl, tlsext_host_name)) {
          Dbg(dbg_ctl_ssl, "using SNI name '%s' for client handshake", tlsext_host_name.get());
        } else {
          Dbg(dbg_ctl_ssl_error, "failed to set SNI name '%s' for client handshake", tlsext_host_name.get());
          Metrics::increment(ssl_rsb.sni_name_set_failure);
        }
      }

      // ALPN
      if (!this->options.alpn_protos.empty()) {
        if (int res = SSL_set_alpn_protos(this->ssl, reinterpret_cast<const uint8_t *>(this->options.alpn_protos.data()),
                                          this->options.alpn_protos.size());
            res != 0) {
          Dbg(dbg_ctl_ssl_error, "failed to set ALPN '%.*s' for client handshake",
              static_cast<int>(this->options.alpn_protos.size()), this->options.alpn_protos.data());
        }
      }
    }

    return sslClientHandShakeEvent(err);

  default:
    ink_assert(0);
    return EVENT_ERROR;
  }
}

int
SSLNetVConnection::sslServerHandShakeEvent(int &err)
{
  // Continue on if we are in the invoked state.  The hook has not yet reenabled
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_CERT_INVOKE || sslHandshakeHookState == HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE ||
      sslHandshakeHookState == HANDSHAKE_HOOKS_PRE_INVOKE || sslHandshakeHookState == HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE) {
    return SSL_WAIT_FOR_HOOK;
  }

  // Go do the preaccept hooks
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_PRE) {
    Metrics::increment(ssl_rsb.total_attempts_handshake_count_in);
    if (!curHook) {
      Dbg(dbg_ctl_ssl, "Initialize preaccept curHook from NULL");
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_VCONN_START_HOOK));
    } else {
      curHook = curHook->next();
    }
    // If no more hooks, move onto CLIENT HELLO

    if (nullptr == curHook) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO;
    } else {
      sslHandshakeHookState = HANDSHAKE_HOOKS_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_START, this);
      return SSL_WAIT_FOR_HOOK;
    }
  }

  // If a blind tunnel was requested in the pre-accept calls, convert.
  // Again no data has been exchanged, so we can go directly
  // without data replay.
  // Note we can't arrive here if a hook is active.

  if (SSL_HOOK_OP_TUNNEL == hookOpRequested) {
    this->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    SSL_free(this->ssl);
    this->ssl = nullptr;
    // Don't mark the handshake as complete yet,
    // Will be checking for that flag not being set after
    // we get out of this callback, and then will shuffle
    // over the buffered handshake packets to the O.S.
    return EVENT_DONE;
  } else if (SSL_HOOK_OP_TERMINATE == hookOpRequested) {
    sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;
    return EVENT_DONE;
  }

  Dbg(dbg_ctl_ssl, "Go on with the handshake state=%s", get_ssl_handshake_hook_state_name(sslHandshakeHookState));

  // All the pre-accept hooks have completed, proceed with the actual accept.
  if (this->handShakeReader) {
    if (BIO_eof(SSL_get_rbio(this->ssl))) { // No more data in the buffer
      // Is this the first read?
      if (!this->handShakeReader->is_read_avail_more_than(0) && !this->handShakeHolder->is_read_avail_more_than(0)) {
#if TS_USE_TLS_ASYNC
        if (SSLConfigParams::async_handshake_enabled) {
          SSL_set_mode(ssl, SSL_MODE_ASYNC);
        }
#endif

        Dbg(dbg_ctl_ssl, "%p first read\n", this);
        // Read from socket to fill in the BIO buffer with the
        // raw handshake data before calling the ssl accept calls.
        int retval = this->read_raw_data();
        if (retval < 0) {
          if (retval == -EAGAIN) {
            // No data at the moment, hang tight
            SSLVCDebug(this, "SSL handshake: EAGAIN");
            return SSL_HANDSHAKE_WANT_READ;
          } else {
            // An error, make us go away
            SSLVCDebug(this, "SSL handshake error: read_retval=%d", retval);
            return EVENT_ERROR;
          }
        } else if (retval == 0) {
          // EOF, go away, we stopped in the handshake
          SSLVCDebug(this, "SSL handshake error: EOF");
          return EVENT_ERROR;
        }
      } else {
        update_rbio(false);
      }
    } // Still data in the BIO
  }

  ssl_error_t ssl_error = this->_ssl_accept();
#if TS_USE_TLS_ASYNC
  if (ssl_error == SSL_ERROR_WANT_ASYNC) {
    // Do we need to set up the async eventfd?  Or is it already registered?
    if (async_ep.fd < 0) {
      size_t numfds;
      OSSL_ASYNC_FD *waitfds;
      // Set up the epoll entry for the signalling
      if (SSL_get_all_async_fds(ssl, nullptr, &numfds) && numfds > 0) {
        // Allocate space for the waitfd on the stack, should only be one most all of the time
        waitfds = reinterpret_cast<OSSL_ASYNC_FD *>(alloca(sizeof(OSSL_ASYNC_FD) * numfds));
        if (SSL_get_all_async_fds(ssl, waitfds, &numfds) && numfds > 0) {
          this->read.triggered  = false;
          this->write.triggered = false;
          // Have to have the read NetState enabled because we are using it for the signal vc
          read.enabled       = true;
          PollDescriptor *pd = get_PollDescriptor(this_ethread());
          this->async_ep.start(pd, waitfds[0], static_cast<NetEvent *>(this), get_NetHandler(this->thread), EVENTIO_READ);
        }
      }
    }
  } else if (SSLConfigParams::async_handshake_enabled) {
    // Make sure the net fd read vio is in the right state
    if (ssl_error == SSL_ERROR_WANT_READ) {
      this->reenable(&read.vio);
      this->read.triggered = 1;
    }
  }
#endif
  if (ssl_error != SSL_ERROR_NONE) {
    err = errno;
    SSLVCDebug(this, "SSL handshake error: %s (%d), errno=%d", SSLErrorName(ssl_error), ssl_error, err);

    char *buf = handShakeBuffer ? handShakeBuffer->buf() : nullptr;
    if (buf && *buf != SSL_OP_HANDSHAKE) {
      SSLVCDebug(this, "SSL hanshake error with bad HS buffer");
      if (getAllowPlain()) {
        SSLVCDebug(this, "Try plain");
        // If this doesn't look like a ClientHello, convert this connection to a UnixNetVC and send the
        // packet for Http Processing
        this->_migrateFromSSL();
        return SSL_RESTART;
      } else if (getTransparentPassThrough()) {
        // start a blind tunnel if tr-pass is set and data does not look like ClientHello
        SSLVCDebug(this, "Data does not look like SSL handshake, starting blind tunnel");
        this->attributes   = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
        sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_ONGOING;
        return EVENT_CONT;
      } else {
        SSLVCDebug(this, "Give up");
      }
    }
  }

  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (dbg_ctl_ssl.on()) {
#ifdef OPENSSL_IS_OPENSSL3
      X509 *cert = SSL_get1_peer_certificate(ssl);
#else
      X509 *cert = SSL_get_peer_certificate(ssl);
#endif

      DbgPrint(dbg_ctl_ssl, "SSL server handshake completed successfully");
      if (cert) {
        debug_certificate_name("client certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("client certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }

    sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;

    if (this->get_tls_handshake_begin_time()) {
      this->_record_tls_handshake_end_time();
      Metrics::increment(ssl_rsb.total_success_handshake_count_in);
    }

    if (this->get_tunnel_type() != SNIRoutingType::NONE) {
      // Foce to use HTTP/1.1 endpoint for SNI Routing
      if (!this->setSelectedProtocol(reinterpret_cast<const unsigned char *>(IP_PROTO_TAG_HTTP_1_1.data()),
                                     IP_PROTO_TAG_HTTP_1_1.size())) {
        return EVENT_ERROR;
      }
    }

    {
      const unsigned char *proto = nullptr;
      unsigned len               = 0;

      increment_ssl_version_metric(SSL_version(ssl));

      // If it's possible to negotiate both NPN and ALPN, then ALPN
      // is preferred since it is the server's preference.  The server
      // preference would not be meaningful if we let the client
      // preference have priority.
      SSL_get0_alpn_selected(ssl, &proto, &len);
      if (len == 0) {
        SSL_get0_next_proto_negotiated(ssl, &proto, &len);
      }

      if (len) {
        if (this->get_tunnel_type() == SNIRoutingType::NONE && !this->setSelectedProtocol(proto, len)) {
          return EVENT_ERROR;
        }
        this->set_negotiated_protocol_id({reinterpret_cast<const char *>(proto), static_cast<size_t>(len)});

        Dbg(dbg_ctl_ssl, "Origin selected next protocol '%.*s'", len, proto);
      } else {
        Dbg(dbg_ctl_ssl, "Origin did not select a next protocol");
      }
    }

#if TS_USE_TLS_ASYNC
    if (SSLConfigParams::async_handshake_enabled) {
      SSL_clear_mode(ssl, SSL_MODE_ASYNC);
      if (async_ep.fd >= 0) {
        async_ep.stop();
      }
    }
#endif
    return EVENT_DONE;

  case SSL_ERROR_WANT_CONNECT:
    return SSL_HANDSHAKE_WANT_CONNECT;

  case SSL_ERROR_WANT_WRITE:
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    return SSL_HANDSHAKE_WANT_READ;
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
  case SSL_ERROR_WANT_CLIENT_HELLO_CB:
    return EVENT_CONT;
#endif
// This value is only defined in openssl has been patched to
// enable the sni callback to break out of the SSL_accept processing
#ifdef SSL_ERROR_WANT_SNI_RESOLVE
  case SSL_ERROR_WANT_X509_LOOKUP:
    return EVENT_CONT;
  case SSL_ERROR_WANT_SNI_RESOLVE:
#elif SSL_ERROR_WANT_X509_LOOKUP
  case SSL_ERROR_WANT_X509_LOOKUP:
#endif
#if defined(SSL_ERROR_WANT_SNI_RESOLVE) || defined(SSL_ERROR_WANT_X509_LOOKUP)
    if (this->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL || SSL_HOOK_OP_TUNNEL == hookOpRequested) {
      this->attributes   = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_ONGOING;
      return EVENT_CONT;
    } else {
      //  Stopping for some other reason, perhaps loading certificate
      return SSL_WAIT_FOR_HOOK;
    }
#endif

#if TS_USE_TLS_ASYNC
  case SSL_ERROR_WANT_ASYNC:
    Metrics::increment(ssl_rsb.error_async);
    return SSL_WAIT_FOR_ASYNC;
#endif

  case SSL_ERROR_WANT_ACCEPT:
    return EVENT_CONT;

  case SSL_ERROR_SSL: {
    SSLVCDebug(this, "SSLNetVConnection::sslServerHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    return EVENT_ERROR;
  }

  case SSL_ERROR_ZERO_RETURN:
    return EVENT_ERROR;
  case SSL_ERROR_SYSCALL:
    return EVENT_ERROR;
  default:
    return EVENT_ERROR;
  }
}

int
SSLNetVConnection::sslClientHandShakeEvent(int &err)
{
  ssl_error_t ssl_error;

  ink_assert(TLSBasicSupport::getInstance(ssl) == this);

  // Initialize properly for a client connection
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_PRE) {
    if (this->pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      // Outbound PROXY Protocol
      VIO &vio        = this->write.vio;
      int64_t ntodo   = vio.ntodo();
      int64_t towrite = vio.buffer.reader()->read_avail();

      if (ntodo > 0 && towrite > 0) {
        MIOBufferAccessor &buf = vio.buffer;
        int needs              = 0;
        int64_t total_written  = 0;
        int64_t r              = super::load_buffer_and_write(towrite, buf, total_written, needs);

        if (total_written > 0) {
          vio.ndone += total_written;
          if (vio.ntodo() != 0) {
            return SSL_WAIT_FOR_HOOK;
          }
        }

        if (r < 0) {
          if (r == -EAGAIN || r == -ENOTCONN || -r == EINPROGRESS) {
            return SSL_WAIT_FOR_HOOK;
          } else {
            return EVENT_ERROR;
          }
        }
      }
    }

    sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE;
  }

  // Do outbound hook processing here
  // Continue on if we are in the invoked state.  The hook has not yet reenabled
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE) {
    return SSL_WAIT_FOR_HOOK;
  }

  // Go do the preaccept hooks
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE) {
    Metrics::increment(ssl_rsb.total_attempts_handshake_count_out);
    if (!curHook) {
      Dbg(dbg_ctl_ssl, "Initialize outbound connect curHook from NULL");
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_START_HOOK));
    } else {
      curHook = curHook->next();
    }
    // If no more hooks, carry on
    if (nullptr != curHook) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_START, this);
      return SSL_WAIT_FOR_HOOK;
    }
  }

  ssl_error = this->_ssl_connect();
  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (dbg_ctl_ssl.on()) {
#ifdef OPENSSL_IS_OPENSSL3
      X509 *cert = SSL_get1_peer_certificate(ssl);
#else
      X509 *cert = SSL_get_peer_certificate(ssl);
#endif

      DbgPrint(dbg_ctl_ssl, "SSL client handshake completed successfully");

      if (cert) {
        debug_certificate_name("server certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("server certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }
    {
      unsigned char const *proto = nullptr;
      unsigned int len           = 0;
      // Make note of the negotiated protocol
      SSL_get0_alpn_selected(ssl, &proto, &len);
      if (len == 0) {
        SSL_get0_next_proto_negotiated(ssl, &proto, &len);
      }
      Dbg(dbg_ctl_ssl_alpn, "Negotiated ALPN: %.*s", len, proto);
      this->set_negotiated_protocol_id({reinterpret_cast<const char *>(proto), static_cast<size_t>(len)});
    }

    // if the handshake is complete and write is enabled reschedule the write
    if (closed == 0 && write.enabled) {
      writeReschedule(nh);
    }

    Metrics::increment(ssl_rsb.total_success_handshake_count_out);

    sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;
    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WANT_WRITE");
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WANT_READ");
    return SSL_HANDSHAKE_WANT_READ;
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
  case SSL_ERROR_WANT_CLIENT_HELLO_CB:
    Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WANT_CLIENT_HELLO_CB");
    break;
#endif
  case SSL_ERROR_WANT_X509_LOOKUP:
    Dbg(dbg_ctl_ssl_error, "SSL_ERROR_WANT_X509_LOOKUP");
    break;

  case SSL_ERROR_WANT_ACCEPT:
    return SSL_HANDSHAKE_WANT_ACCEPT;

  case SSL_ERROR_WANT_CONNECT:
    break;

  case SSL_ERROR_ZERO_RETURN:
    Dbg(dbg_ctl_ssl_error, "EOS");
    return EVENT_ERROR;

  case SSL_ERROR_SYSCALL:
    err = errno;
    Metrics::increment(ssl_rsb.error_syscall);
    Dbg(dbg_ctl_ssl_error, "syscall");
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SSL:
  default: {
    err = (errno) ? errno : -ENET_SSL_CONNECT_FAILED;
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    // FIXME -- This triggers a retry on cases of cert validation errors...
    SSLVCDebug(this, "SSL_ERROR_SSL errno=%d", errno);
    Metrics::increment(ssl_rsb.error_ssl);
    Dbg(dbg_ctl_ssl_error, "SSL_ERROR_SSL");
    if (e) {
      if (this->options.sni_servername) {
        Dbg(dbg_ctl_ssl_error, "SSL connection failed for '%s': %s", this->options.sni_servername.get(), buf);
      } else {
        char buff[INET6_ADDRSTRLEN];
        ats_ip_ntop(this->get_remote_addr(), buff, INET6_ADDRSTRLEN);
        Dbg(dbg_ctl_ssl_error, "SSL connection failed for '%s': %s", buff, buf);
      }
    }
    return EVENT_ERROR;
  } break;
  }
  return EVENT_CONT;
}

void
SSLNetVConnection::reenable(NetHandler *nh, int event)
{
  Dbg(dbg_ctl_ssl, "Handshake reenable from state=%s", get_ssl_handshake_hook_state_name(sslHandshakeHookState));

  // Mark as error to stop the Handshake
  if (event == TS_EVENT_ERROR) {
    sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_ERROR;
  }

  switch (sslHandshakeHookState) {
  case HANDSHAKE_HOOKS_PRE_INVOKE:
    sslHandshakeHookState = HANDSHAKE_HOOKS_PRE;
    break;
  case HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
    sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE;
    break;
  case HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO;
    break;
  case HANDSHAKE_HOOKS_CERT_INVOKE:
    sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
    break;
  case HANDSHAKE_HOOKS_VERIFY_SERVER:
  case HANDSHAKE_HOOKS_CLIENT_CERT:
    break;
  default:
    break;
  }

  // Reenabling from the handshake callback
  //
  // Originally, we would wait for the callback to go again to execute additional
  // hooks, but since the callbacks are associated with the context and the context
  // can be replaced by the plugin, it didn't seem reasonable to assume that the
  // callback would be executed again.  So we walk through the rest of the hooks
  // here in the reenable.
  if (curHook != nullptr) {
    curHook = curHook->next();
    Dbg(dbg_ctl_ssl, "iterate from reenable curHook=%p", curHook);
  }
  if (curHook != nullptr) {
    // Invoke the hook and return, wait for next reenable
    if (sslHandshakeHookState == HANDSHAKE_HOOKS_CLIENT_HELLO) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE;
      curHook->invoke(TS_EVENT_SSL_CLIENT_HELLO, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_CLIENT_CERT) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE;
      curHook->invoke(TS_EVENT_SSL_VERIFY_CLIENT, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_CERT) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_CERT_INVOKE;
      curHook->invoke(TS_EVENT_SSL_CERT, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_SNI) {
      curHook->invoke(TS_EVENT_SSL_SERVERNAME, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_PRE) {
      Dbg(dbg_ctl_ssl, "Reenable preaccept");
      sslHandshakeHookState = HANDSHAKE_HOOKS_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_START, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE) {
      Dbg(dbg_ctl_ssl, "Reenable outbound connect");
      sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_START, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_DONE) {
      if (this->get_context() == NET_VCONNECTION_OUT) {
        ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_CLOSE, this);
      } else {
        ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_CLOSE, this);
      }
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_VERIFY_SERVER) {
      Dbg(dbg_ctl_ssl, "ServerVerify");
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_SSL_VERIFY_SERVER, this);
    }
    return;
  } else {
    // Move onto the "next" state
    switch (this->sslHandshakeHookState) {
    case HANDSHAKE_HOOKS_PRE:
    case HANDSHAKE_HOOKS_PRE_INVOKE:
      sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO;
      break;
    case HANDSHAKE_HOOKS_CLIENT_HELLO:
    case HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
      sslHandshakeHookState = HANDSHAKE_HOOKS_SNI;
      break;
    case HANDSHAKE_HOOKS_SNI:
      sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
      break;
    case HANDSHAKE_HOOKS_CERT:
    case HANDSHAKE_HOOKS_CERT_INVOKE:
      sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_CERT;
      break;
    case HANDSHAKE_HOOKS_OUTBOUND_PRE:
    case HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
      this->write.triggered = true;
      this->write.enabled   = true;
      this->writeReschedule(nh);
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      break;
    case HANDSHAKE_HOOKS_CLIENT_CERT:
    case HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      break;
    case HANDSHAKE_HOOKS_VERIFY_SERVER:
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      break;
    default:
      break;
    }
    Dbg(dbg_ctl_ssl, "iterate from reenable curHook=%p %s", curHook, get_ssl_handshake_hook_state_name(sslHandshakeHookState));
  }

  this->readReschedule(nh);
}

bool
SSLNetVConnection::callHooks(TSEvent eventId)
{
  // Only dealing with the SNI/CERT hook so far.
  ink_assert(eventId == TS_EVENT_SSL_CLIENT_HELLO || eventId == TS_EVENT_SSL_CERT || eventId == TS_EVENT_SSL_SERVERNAME ||
             eventId == TS_EVENT_SSL_VERIFY_SERVER || eventId == TS_EVENT_SSL_VERIFY_CLIENT || eventId == TS_EVENT_VCONN_CLOSE ||
             eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE);
  Dbg(dbg_ctl_ssl, "sslHandshakeHookState=%s eventID=%d", get_ssl_handshake_hook_state_name(this->sslHandshakeHookState), eventId);

  // Move state if it is appropriate
  if (eventId == TS_EVENT_VCONN_CLOSE) {
    // Regardless of state, if the connection is closing, then transition to
    // the DONE state. This will trigger us to call the appropriate cleanup
    // routines.
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
  } else {
    switch (this->sslHandshakeHookState) {
    case HANDSHAKE_HOOKS_PRE:
    case HANDSHAKE_HOOKS_OUTBOUND_PRE:
      if (eventId == TS_EVENT_SSL_CLIENT_HELLO) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO;
      } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_SNI;
      } else if (eventId == TS_EVENT_SSL_VERIFY_SERVER) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_VERIFY_SERVER;
      } else if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
      }
      break;
    case HANDSHAKE_HOOKS_CLIENT_HELLO:
      if (eventId == TS_EVENT_SSL_SERVERNAME) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_SNI;
      } else if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
      }
      break;
    case HANDSHAKE_HOOKS_SNI:
      if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
      }
      break;
    default:
      break;
    }
  }

  // Look for hooks associated with the event
  switch (this->sslHandshakeHookState) {
  case HANDSHAKE_HOOKS_CLIENT_HELLO:
  case HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    if (!curHook) {
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_SSL_CLIENT_HELLO_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (curHook == nullptr) {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_SNI;
    } else {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE;
    }
    break;
  case HANDSHAKE_HOOKS_VERIFY_SERVER:
    // The server verify event addresses ATS to origin handshake
    // All the other events are for client to ATS
    if (!curHook) {
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_SSL_VERIFY_SERVER_HOOK));
    } else {
      curHook = curHook->next();
    }
    break;
  case HANDSHAKE_HOOKS_SNI:
    if (!curHook) {
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_SSL_SERVERNAME_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (!curHook) {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
    }
    break;
  case HANDSHAKE_HOOKS_CERT:
  case HANDSHAKE_HOOKS_CERT_INVOKE:
    if (!curHook) {
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_SSL_CERT_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (curHook == nullptr) {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_CLIENT_CERT;
    } else {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT_INVOKE;
    }
    break;
  case HANDSHAKE_HOOKS_CLIENT_CERT:
  case HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
    if (!curHook) {
      curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_SSL_VERIFY_CLIENT_HOOK));
    } else {
      curHook = curHook->next();
    }
  // fallthrough
  case HANDSHAKE_HOOKS_DONE:
  case HANDSHAKE_HOOKS_OUTBOUND_PRE:
    if (eventId == TS_EVENT_VCONN_CLOSE) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_VCONN_CLOSE_HOOK));
      } else {
        curHook = curHook->next();
      }
    } else if (eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = g_ssl_hooks->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_CLOSE_HOOK));
      } else {
        curHook = curHook->next();
      }
    }
    break;
  default:
    curHook                     = nullptr;
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    return true;
  }

  Dbg(dbg_ctl_ssl, "iterated to curHook=%p", curHook);

  bool reenabled = true;

  if (SSL_HOOK_OP_TUNNEL == hookOpRequested) {
    this->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    // Don't mark the handshake as complete yet,
    // Will be checking for that flag not being set after
    // we get out of this callback, and then will shuffle
    // over the buffered handshake packets to the O.S.
    // sslHandShakeComplete = 1;
    return reenabled;
  }

  if (curHook != nullptr) {
    WEAK_SCOPED_MUTEX_LOCK(lock, curHook->m_cont->mutex, this_ethread());
    curHook->invoke(eventId, this);
    reenabled =
      (this->sslHandshakeHookState != HANDSHAKE_HOOKS_CERT_INVOKE && this->sslHandshakeHookState != HANDSHAKE_HOOKS_PRE_INVOKE &&
       this->sslHandshakeHookState != HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE);
    Dbg(dbg_ctl_ssl, "Called hook on state=%s reenabled=%d", get_ssl_handshake_hook_state_name(sslHandshakeHookState), reenabled);
  }

  return reenabled;
}

int
SSLNetVConnection::populate(Connection &con, Continuation *c, void *arg)
{
  int retval = super::populate(con, c, arg);
  if (retval != EVENT_DONE) {
    return retval;
  }
  // Add in the SSL data
  this->ssl = static_cast<SSL *>(arg);
  // Maybe bring over the stats?

  sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;
  this->_bindSSLObject();
  return EVENT_DONE;
}

void
SSLNetVConnection::increment_ssl_version_metric(int version) const
{
  switch (version) {
  case SSL3_VERSION:
    Metrics::increment(ssl_rsb.total_sslv3);
    break;
  case TLS1_VERSION:
    Metrics::increment(ssl_rsb.total_tlsv1);
    break;
  case TLS1_1_VERSION:
    Metrics::increment(ssl_rsb.total_tlsv11);
    break;
  case TLS1_2_VERSION:
    Metrics::increment(ssl_rsb.total_tlsv12);
    break;
#ifdef TLS1_3_VERSION
  case TLS1_3_VERSION:
    Metrics::increment(ssl_rsb.total_tlsv13);
    break;
#endif
  default:
    Dbg(dbg_ctl_ssl, "Unrecognized SSL version %d", version);
    break;
  }
}

std::string_view
SSLNetVConnection::map_tls_protocol_to_tag(const char *proto_string) const
{
  std::string_view retval{"tls/?.?"sv}; // return this if the protocol lookup doesn't work.

  if (proto_string) {
    // openSSL guarantees the case of the protocol string.
    if (proto_string[0] == 'T' && proto_string[1] == 'L' && proto_string[2] == 'S' && proto_string[3] == 'v' &&
        proto_string[4] == '1') {
      if (proto_string[5] == 0) {
        retval = IP_PROTO_TAG_TLS_1_0;
      } else if (proto_string[5] == '.' && proto_string[7] == 0) {
        switch (proto_string[6]) {
        case '1':
          retval = IP_PROTO_TAG_TLS_1_1;
          break;
        case '2':
          retval = IP_PROTO_TAG_TLS_1_2;
          break;
        case '3':
          retval = IP_PROTO_TAG_TLS_1_3;
          break;
        default:
          break;
        }
      }
    }
  }
  return retval;
}

int
SSLNetVConnection::populate_protocol(std::string_view *results, int n) const
{
  int retval = 0;
  if (n > retval) {
    results[retval] = map_tls_protocol_to_tag(this->get_tls_protocol_name());
    if (!results[retval].empty()) {
      ++retval;
    }
    if (n > retval) {
      retval += super::populate_protocol(results + retval, n - retval);
    }
  }
  return retval;
}

const char *
SSLNetVConnection::protocol_contains(std::string_view prefix) const
{
  const char *retval   = nullptr;
  std::string_view tag = map_tls_protocol_to_tag(this->get_tls_protocol_name());
  if (prefix.size() <= tag.size() && strncmp(tag.data(), prefix.data(), prefix.size()) == 0) {
    retval = tag.data();
  } else {
    retval = super::protocol_contains(prefix);
  }
  return retval;
}

void
SSLNetVConnection::_fire_ssl_servername_event()
{
  this->callHooks(TS_EVENT_SSL_SERVERNAME);
}

in_port_t
SSLNetVConnection::_get_local_port()
{
  return this->get_local_port();
}

bool
SSLNetVConnection::_isTryingRenegotiation() const
{
  if (SSLConfigParams::ssl_allow_client_renegotiation == false && this->getSSLHandShakeComplete()) {
    return true;
  } else {
    return false;
  }
}

shared_SSL_CTX
SSLNetVConnection::_lookupContextByName(const std::string &servername, SSLCertContextType ctxType)
{
  shared_SSL_CTX ctx = nullptr;
  SSLCertificateConfig::scoped_config lookup;
  SSLCertContext *cc = lookup->find(servername, ctxType);

  if (cc) {
    ctx = cc->getCtx();
  }

  if (cc && ctx && SSLCertContextOption::OPT_TUNNEL == cc->opt && this->get_is_transparent()) {
    this->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    this->setSSLHandShakeComplete(SSLHandshakeStatus::SSL_HANDSHAKE_DONE);
    return nullptr;
  } else {
    return ctx;
  }
}

shared_SSL_CTX
SSLNetVConnection::_lookupContextByIP()
{
  shared_SSL_CTX ctx = nullptr;
  SSLCertificateConfig::scoped_config lookup;
  IpEndpoint ip;
  int namelen = sizeof(ip);

  // Return null if this vc is already configured as a tunnel
  if (this->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
    return nullptr;
  }

  SSLCertContext *cc = nullptr;
  if (this->get_is_proxy_protocol() && this->get_proxy_protocol_version() != ProxyProtocolVersion::UNDEFINED) {
    ip.sa = *(this->get_proxy_protocol_dst_addr());
    ip_port_text_buffer ipb1;
    ats_ip_nptop(&ip, ipb1, sizeof(ipb1));
    cc = lookup->find(ip);
    if (dbg_ctl_proxyprotocol.on()) {
      IpEndpoint src;
      ip_port_text_buffer ipb2;
      int ip_len = sizeof(src);

      if (0 != safe_getpeername(this->get_socket(), &src.sa, &ip_len)) {
        DbgPrint(dbg_ctl_proxyprotocol, "Failed to get src ip, errno = [%d]", errno);
        return nullptr;
      }
      ats_ip_nptop(&src, ipb2, sizeof(ipb2));
      DbgPrint(dbg_ctl_proxyprotocol, "IP context is %p for [%s] -> [%s], default context %p", cc, ipb2, ipb1,
               lookup->defaultContext());
    }
  } else if (0 == safe_getsockname(this->get_socket(), &ip.sa, &namelen)) {
    cc = lookup->find(ip);
  }
  if (cc) {
    ctx = cc->getCtx();
  }

  return ctx;
}

void
SSLNetVConnection::set_ca_cert_file(std::string_view file, std::string_view dir)
{
  if (file.size()) {
    char *n = new char[file.size() + 1];
    std::memcpy(n, file.data(), file.size());
    n[file.size()] = '\0';
    _ca_cert_file.reset(n);
  }
  if (dir.size()) {
    char *n = new char[dir.size() + 1];
    std::memcpy(n, dir.data(), dir.size());
    n[dir.size()] = '\0';
    _ca_cert_dir.reset(n);
  }
}

void *
SSLNetVConnection::_prepareForMigration()
{
  SSL *save_ssl = this->ssl;

  this->_unbindSSLObject();
  this->ssl = nullptr;

  return save_ssl;
}

NetProcessor *
SSLNetVConnection::_getNetProcessor()
{
  return &sslNetProcessor;
}

void
SSLNetVConnection::_propagateHandShakeBuffer(UnixNetVConnection *target, EThread *t)
{
  Debug("ssl", "allow-plain, handshake buffer ready to read=%" PRId64, this->handShakeHolder->read_avail());
  // Take ownership of the handShake buffer
  this->sslHandshakeStatus = SSLHandshakeStatus::SSL_HANDSHAKE_DONE;
  NetState *s              = &target->read;
  s->vio.buffer.writer_for(this->handShakeBuffer);
  s->vio.set_reader(this->handShakeHolder);
  this->handShakeHolder = nullptr;
  this->handShakeBuffer = nullptr;
  s->vio.vc_server      = target;
  s->vio.cont           = this->read.vio.cont;
  s->vio.mutex          = this->read.vio.cont->mutex;
  // Passing along the buffer, don't keep a reading holding early in the buffer
  this->handShakeReader->dealloc();
  this->handShakeReader = nullptr;

  // Kick things again, so the data that was copied into the
  // vio.read buffer gets processed
  target->readSignalDone(VC_EVENT_READ_COMPLETE, get_NetHandler(t));
}

/*
 * Replaces the current SSLNetVConnection with a UnixNetVConnection
 * Propagates any data in the SSL handShakeBuffer to be processed
 * by the UnixNetVConnection logic
 */
UnixNetVConnection *
SSLNetVConnection::_migrateFromSSL()
{
  EThread *t            = this_ethread();
  NetHandler *client_nh = get_NetHandler(t);
  ink_assert(client_nh);

  Connection hold_con;
  hold_con.move(this->con);

  // We will leave the SSL object with the original SSLNetVC to be
  // cleaned up.  Only moving the socket and handShakeBuffer
  // So no need to call _prepareMigration

  // Do_io_close will signal the VC to be freed on the original thread
  // Since we moved the con context, the fd will not be closed
  // Go ahead and remove the fd from the original thread's epoll structure, so it is not
  // processed on two threads simultaneously
  this->ep.stop();

  // Create new VC:
  UnixNetVConnection *newvc = static_cast<UnixNetVConnection *>(unix_netProcessor.allocate_vc(t));
  ink_assert(newvc != nullptr);
  if (newvc != nullptr && newvc->populate(hold_con, this->read.vio.cont, nullptr) != EVENT_DONE) {
    newvc->do_io_close();
    Debug("ssl", "Failed to populate unixvc for allow-plain");
    newvc = nullptr;
  }
  if (newvc != nullptr) {
    newvc->attributes = HttpProxyPort::TRANSPORT_DEFAULT;
    newvc->set_is_transparent(this->is_transparent);
    newvc->set_context(get_context());
    newvc->options = this->options;
    Debug("ssl", "Move to unixvc for allow-plain");
    this->_propagateHandShakeBuffer(newvc, t);
  }

  // Do not mark this closed until the end so it does not get freed by the other thread too soon
  this->do_io_close();
  return newvc;
}

ssl_curve_id
SSLNetVConnection::_get_tls_curve() const
{
  if (getSSLSessionCacheHit()) {
    return getSSLCurveNID();
  } else {
    return SSLGetCurveNID(ssl);
  }
}

ssl_error_t
SSLNetVConnection::_ssl_accept()
{
  ERR_clear_error();

  int ret       = 0;
  int ssl_error = SSL_ERROR_NONE;

#if TS_HAS_TLS_EARLY_DATA
  if (!this->_early_data_finish) {
#if HAVE_SSL_READ_EARLY_DATA
    size_t nread = 0;
#else
    ssize_t nread = 0;
#endif

    while (true) {
      bool had_error_on_reading_early_data = false;
      bool finished_reading_early_data     = false;
      IOBufferBlock *block                 = new_IOBufferBlock();
      block->alloc(BUFFER_SIZE_INDEX_16K);

#if HAVE_SSL_READ_EARLY_DATA
      ret = SSL_read_early_data(ssl, block->buf(), index_to_buffer_size(BUFFER_SIZE_INDEX_16K), &nread);
      if (ret == SSL_READ_EARLY_DATA_ERROR) {
        had_error_on_reading_early_data = true;
      } else if (ret == SSL_READ_EARLY_DATA_FINISH) {
        finished_reading_early_data = true;
      }
#else
      // If SSL_read_early_data is unavailable, it's probably BoringSSL,
      // and SSL_in_early_data should be available.
      ret = SSL_accept(ssl);
      if (ret <= 0) {
        had_error_on_reading_early_data = true;
      } else {
        if (SSL_in_early_data(ssl)) {
          ret                         = SSL_read(ssl, block->buf(), index_to_buffer_size(BUFFER_SIZE_INDEX_16K));
          finished_reading_early_data = !SSL_in_early_data(ssl);
          if (ret < 0) {
            nread = 0;
            if (finished_reading_early_data) {
              ret = 2; // SSL_READ_EARLY_DATA_FINISH
            } else {
              // Don't override ret here.
              // Keeping the original retrurn value let ATS allow to check the value by SSL_get_error.
              // That gives a chance to progress handshake process, or shutdown a connection if the error is serious.
              had_error_on_reading_early_data = true;
            }
          } else {
            nread = ret;
            if (finished_reading_early_data) {
              ret = 2; // SSL_READ_EARLY_DATA_FINISH
            } else {
              ret = 1; // SSL_READ_EARLY_DATA_SUCCESS
            }
          }
        } else {
          nread                       = 0;
          ret                         = 2; // SSL_READ_EARLY_DATA_FINISH
          finished_reading_early_data = true;
        }
      }
#endif

      if (had_error_on_reading_early_data) {
        Dbg(dbg_ctl_ssl_early_data, "Error on reading early data: %d", ret);
        block->free();
        break;
      } else {
        if (nread > 0) {
          if (this->_early_data_buf == nullptr) {
            this->_early_data_buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_16K);
            this->_early_data_reader = this->_early_data_buf->alloc_reader();
          }
          block->fill(nread);
          this->_early_data_buf->append_block(block);
          Metrics::increment(ssl_rsb.early_data_received_count);

          if (dbg_ctl_ssl_early_data_show_received.on()) {
            std::string early_data_str(reinterpret_cast<char *>(block->buf()), nread);
            DbgPrint(dbg_ctl_ssl_early_data_show_received, "Early data buffer: \n%s", early_data_str.c_str());
          }
        } else {
          block->free();
        }

        if (finished_reading_early_data) {
          this->_early_data_finish = true;
          Dbg(dbg_ctl_ssl_early_data, "SSL_READ_EARLY_DATA_FINISH: size = %lu", nread);

          if (this->_early_data_reader == nullptr || this->_early_data_reader->read_avail() == 0) {
            Dbg(dbg_ctl_ssl_early_data, "no data in early data buffer");
            ERR_clear_error();
            ret = SSL_accept(ssl);
          }
          break;
        }
        Dbg(dbg_ctl_ssl_early_data, "SSL_READ_EARLY_DATA_SUCCESS: size = %lu", nread);
      }
    }
  } else {
    ret = SSL_accept(ssl);
  }
#else
  ret = SSL_accept(ssl);
#endif

  if (ret > 0) {
    return SSL_ERROR_NONE;
  }
  ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && dbg_ctl_ssl_error_accept.on()) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    DbgPrint(dbg_ctl_ssl_error_accept, "SSL accept returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }

  return ssl_error;
}

ssl_error_t
SSLNetVConnection::_ssl_connect()
{
  ERR_clear_error();

  SSL_SESSION *sess = SSL_get_session(ssl);
  if (first_ssl_connect) {
    first_ssl_connect = false;
    if (!sess && SSLConfigParams::origin_session_cache == 1 && SSLConfigParams::origin_session_cache_size > 0) {
      std::string sni_addr = get_sni_addr(ssl);
      if (!sni_addr.empty()) {
        std::string lookup_key;
        swoc::bwprint(lookup_key, "{}:{}:{}", sni_addr.c_str(), SSL_get_SSL_CTX(ssl), get_verify_str(ssl));

        Dbg(dbg_ctl_ssl_origin_session_cache, "origin session cache lookup key = %s", lookup_key.c_str());

        std::shared_ptr<SSL_SESSION> shared_sess = this->getOriginSession(ssl, lookup_key);

        if (shared_sess && SSL_set_session(ssl, shared_sess.get())) {
          // Keep a reference of this shared pointer in the connection
          this->client_sess = shared_sess;
        }
      }
    }
  }

  int ret = SSL_connect(ssl);

  if (ret > 0) {
    if (SSL_session_reused(ssl)) {
      Metrics::increment(ssl_rsb.origin_session_reused_count);
      Dbg(dbg_ctl_ssl_origin_session_cache, "reused session to origin server");
    } else {
      Dbg(dbg_ctl_ssl_origin_session_cache, "new session to origin server");
    }
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && dbg_ctl_ssl_error_connect.on()) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    DbgPrint(dbg_ctl_ssl_error_connect, "SSL connect returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }

  return ssl_error;
}

ssl_error_t
SSLNetVConnection::_ssl_write_buffer(const void *buf, int64_t nbytes, int64_t &nwritten)
{
  nwritten = 0;

  if (unlikely(nbytes == 0)) {
    return SSL_ERROR_NONE;
  }
  ERR_clear_error();

  int ret;
  // If SSL_write_early_data is available, it's probably OpenSSL,
  // and SSL_is_init_finished should be available.
  // If SSL_write_early_data is unavailable, its' probably BoringSSL,
  // and we can use SSL_write to send early data.
#if TS_HAS_TLS_EARLY_DATA
  if (SSL_version(ssl) >= TLS1_3_VERSION) {
#ifdef HAVE_SSL_WRITE_EARLY_DATA
    if (SSL_is_init_finished(ssl)) {
#endif
      ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
#ifdef HAVE_SSL_WRITE_EARLY_DATA
    } else {
      size_t nwrite;
      ret = SSL_write_early_data(ssl, buf, static_cast<size_t>(nbytes), &nwrite);
      if (ret == 1) {
        ret = nwrite;
      }
    }
#endif
  } else {
    ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
  }
#else
  ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
#endif

  if (ret > 0) {
    nwritten = ret;
    BIO *bio = SSL_get_wbio(ssl);
    if (bio != nullptr) {
      (void)BIO_flush(bio);
    }
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && dbg_ctl_ssl_error_write.on()) {
    char tempbuf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, tempbuf, sizeof(tempbuf));
    DbgPrint(dbg_ctl_ssl_error_write, "SSL write returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, tempbuf);
  }
  return ssl_error;
}

ssl_error_t
SSLNetVConnection::_ssl_read_buffer(void *buf, int64_t nbytes, int64_t &nread)
{
  nread = 0;

  if (unlikely(nbytes == 0)) {
    return SSL_ERROR_NONE;
  }
  ERR_clear_error();

#if TS_HAS_TLS_EARLY_DATA
  if (SSL_version(ssl) >= TLS1_3_VERSION) {
    int64_t early_data_len = 0;
    if (this->_early_data_reader != nullptr) {
      early_data_len = this->_early_data_reader->read_avail();
    }

    if (early_data_len > 0) {
      Dbg(dbg_ctl_ssl_early_data, "Reading from early data buffer.");
      this->_increment_early_data_len(this->_early_data_reader->read(buf, nbytes < early_data_len ? nbytes : early_data_len));

      if (nbytes < early_data_len) {
        nread = nbytes;
      } else {
        nread = early_data_len;
      }

      return SSL_ERROR_NONE;
    }

    bool early_data_enabled = this->hints_from_sni.server_max_early_data.has_value() ?
                                this->hints_from_sni.server_max_early_data.value() > 0 :
                                SSLConfigParams::server_max_early_data > 0;
    if (early_data_enabled && !this->_early_data_finish) {
      bool had_error_on_reading_early_data = false;
      bool finished_reading_early_data     = false;
      Dbg(dbg_ctl_ssl_early_data, "More early data to read.");
      ssl_error_t ssl_error = SSL_ERROR_NONE;
      int ret;
#if HAVE_SSL_READ_EARLY_DATA
      size_t read_bytes = 0;
#else
      ssize_t read_bytes = 0;
#endif

#ifdef HAVE_SSL_READ_EARLY_DATA
      ret = SSL_read_early_data(ssl, buf, static_cast<size_t>(nbytes), &read_bytes);
      if (ret == SSL_READ_EARLY_DATA_ERROR) {
        had_error_on_reading_early_data = true;
        ssl_error                       = SSL_get_error(ssl, ret);
      } else if (ret == SSL_READ_EARLY_DATA_FINISH) {
        finished_reading_early_data = true;
      }
#else
      // If SSL_read_early_data is unavailable, it's probably OpenSSL,
      // and SSL_in_early_data should be available.
      if (SSL_in_early_data(ssl)) {
        ret                         = SSL_read(ssl, buf, nbytes);
        finished_reading_early_data = !SSL_in_early_data(ssl);
        if (ret < 0) {
          if (!finished_reading_early_data) {
            had_error_on_reading_early_data = true;
            ssl_error                       = SSL_get_error(ssl, ret);
          }
          read_bytes = 0;
        } else {
          read_bytes = ret;
        }
      } else {
        finished_reading_early_data = true;
        read_bytes                  = 0;
      }
#endif

      if (had_error_on_reading_early_data) {
        Dbg(dbg_ctl_ssl_early_data, "Error reading early data: %s", ERR_error_string(ERR_get_error(), nullptr));
      } else {
        if ((nread = read_bytes) > 0) {
          this->_increment_early_data_len(read_bytes);
          Metrics::increment(ssl_rsb.early_data_received_count);
          if (dbg_ctl_ssl_early_data_show_received.on()) {
            std::string early_data_str(reinterpret_cast<char *>(buf), nread);
            DbgPrint(dbg_ctl_ssl_early_data_show_received, "Early data buffer: \n%s", early_data_str.c_str());
          }
        }

        if (finished_reading_early_data) {
          this->_early_data_finish = true;
          Dbg(dbg_ctl_ssl_early_data, "SSL_READ_EARLY_DATA_FINISH: size = %" PRId64, nread);
        } else {
          Dbg(dbg_ctl_ssl_early_data, "SSL_READ_EARLY_DATA_SUCCESS: size = %" PRId64, nread);
        }
      }
      return ssl_error;
    }
  }
#endif

  int ret = SSL_read(ssl, buf, static_cast<int>(nbytes));
  if (ret > 0) {
    nread = ret;
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && dbg_ctl_ssl_error_read.on()) {
    char tempbuf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, tempbuf, sizeof(tempbuf));
    DbgPrint(dbg_ctl_ssl_error_read, "SSL read returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, tempbuf);
  }

  return ssl_error;
}

void
SSLNetVConnection::set_valid_tls_protocols(unsigned long proto_mask, unsigned long max_mask)
{
  SSL_set_options(this->ssl, proto_mask);
  SSL_clear_options(this->ssl, max_mask & ~proto_mask);
}

void
SSLNetVConnection::set_valid_tls_version_min(int min)
{
  // Ignore available versions set by SSL_(CTX_)set_options if a ragne is specified
  SSL_clear_options(this->ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);

  int ver = 0;
  if (min >= 0) {
    ver = TLS1_VERSION + min;
  }
  SSL_set_min_proto_version(this->ssl, ver);
}

void
SSLNetVConnection::set_valid_tls_version_max(int max)
{
  // Ignore available versions set by SSL_(CTX_)set_options if a ragne is specified
  SSL_clear_options(this->ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);

  int ver = 0;
  if (max >= 0) {
    ver = TLS1_VERSION + max;
  }
  SSL_set_max_proto_version(this->ssl, ver);
}

void
SSLNetVConnection::update_early_data_config(uint32_t max_early_data, uint32_t recv_max_early_data)
{
#if TS_HAS_TLS_EARLY_DATA
  // Must disable OpenSSL's internal anti-replay if external cache is used with
  // 0-rtt, otherwise session reuse will be broken. The freshness check described
  // in https://tools.ietf.org/html/rfc8446#section-8.3 is still performed. But we
  // still need to implement something to try to prevent replay atacks.
  //
  // We are now also disabling this when using OpenSSL's internal cache, since we
  // are calling "ssl_accept" non-blocking, it seems to be confusing the anti-replay
  // mechanism and causing session resumption to fail.
#ifdef HAVE_SSL_SET_MAX_EARLY_DATA
  bool ret1 = false;
  bool ret2 = false;
  if ((ret1 = SSL_set_max_early_data(ssl, max_early_data)) == 1) {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_max_early_data %u: success", max_early_data);
  } else {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_max_early_data %u: failed", max_early_data);
  }

  if ((ret2 = SSL_set_recv_max_early_data(ssl, recv_max_early_data)) == 1) {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_recv_max_early_data %u: success", recv_max_early_data);
  } else {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_recv_max_early_data %u: failed", recv_max_early_data);
  }

  if (ret1 && ret2) {
    Dbg(dbg_ctl_ssl_early_data, "Must disable anti-replay if 0-rtt is enabled.");
    SSL_set_options(ssl, SSL_OP_NO_ANTI_REPLAY);
  }
#else
  // If SSL_set_max_early_data is unavailable, it's probably BoringSSL,
  // and SSL_set_early_data_enabled should be available.
  bool const early_data_enabled = max_early_data > 0 ? 1 : 0;
  SSL_set_early_data_enabled(ssl, early_data_enabled);
  Debug("ssl", "Called SSL_set_early_data_enabled with %d", early_data_enabled);
#endif
#endif
}
