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
#include "records/I_RecHttp.h"

#include "InkAPIInternal.h" // Added to include the ssl_hook definitions
#include "Log.h"
#include "HttpTunnel.h"
#include "ProxyProtocol.h"
#include "HttpConfig.h"

#include "P_Net.h"
#include "P_SSLNextProtocolSet.h"
#include "P_SSLUtils.h"
#include "P_SSLConfig.h"
#include "P_SSLClientUtils.h"
#include "P_SSLSNI.h"
#include "BIO_fastopen.h"
#include "SSLStats.h"
#include "SSLInternal.h"

#include <climits>
#include <string>

using namespace std::literals;

#if TS_USE_TLS_ASYNC
#include <openssl/async.h>
#endif

// This is missing from BoringSSL
#ifndef BIO_eof
#define BIO_eof(b) (int)BIO_ctrl(b, BIO_CTRL_EOF, 0, nullptr)
#endif

#define SSL_READ_ERROR_NONE 0
#define SSL_READ_ERROR 1
#define SSL_READ_READY 2
#define SSL_READ_COMPLETE 3
#define SSL_READ_WOULD_BLOCK 4
#define SSL_READ_EOS 5
#define SSL_HANDSHAKE_WANT_READ 6
#define SSL_HANDSHAKE_WANT_WRITE 7
#define SSL_HANDSHAKE_WANT_ACCEPT 8
#define SSL_HANDSHAKE_WANT_CONNECT 9
#define SSL_WRITE_WOULD_BLOCK 10
#define SSL_WAIT_FOR_HOOK 11
#define SSL_WAIT_FOR_ASYNC 12

ClassAllocator<SSLNetVConnection> sslNetVCAllocator("sslNetVCAllocator");

namespace
{
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

static SSL *
make_ssl_connection(SSL_CTX *ctx, SSLNetVConnection *netvc)
{
  SSL *ssl;

  if (likely(ssl = SSL_new(ctx))) {
    netvc->ssl = ssl;

    // Only set up the bio stuff for the server side
    if (netvc->get_context() == NET_VCONNECTION_OUT) {
      BIO *bio = BIO_new(const_cast<BIO_METHOD *>(BIO_s_fastopen()));
      BIO_set_fd(bio, netvc->get_socket(), BIO_NOCLOSE);

      if (netvc->options.f_tcp_fastopen) {
        BIO_set_conn_address(bio, netvc->get_remote_addr());
      }

      SSL_set_bio(ssl, bio, bio);
    } else {
      netvc->initialize_handshake_buffers();
      BIO *rbio = BIO_new(BIO_s_mem());
      BIO *wbio = BIO_new_fd(netvc->get_socket(), BIO_NOCLOSE);
      BIO_set_mem_eof_return(wbio, -1);
      SSL_set_bio(ssl, rbio, wbio);
    }

    SSLNetVCAttach(ssl, netvc);
  }

  return ssl;
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
    Debug("ssl", "%s %.*s", msg, (int)len, ptr);
  }

  BIO_free(bio);
}

static int
ssl_read_from_net(SSLNetVConnection *sslvc, EThread *lthread, int64_t &ret)
{
  NetState *s            = &sslvc->read;
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

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] b->write_avail()=%" PRId64, amount_to_read);
    char *current_block = buf.writer()->end();
    ink_release_assert(current_block != nullptr);
    sslErr = SSLReadBuffer(sslvc->ssl, current_block, amount_to_read, nread);

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] nread=%d", (int)nread);

    switch (sslErr) {
    case SSL_ERROR_NONE:

#if DEBUG
      SSLDebugBufferPrint("ssl_buff", current_block, nread, "SSL Read");
#endif
      ink_assert(nread);
      bytes_read += nread;
      if (nread > 0) {
        buf.writer()->fill(nread); // Tell the buffer, we've used the bytes
        sslvc->netActivity(lthread);
      }
      break;
    case SSL_ERROR_WANT_WRITE:
      event = SSL_WRITE_WOULD_BLOCK;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(write)");
      break;
    case SSL_ERROR_WANT_READ:
      event = SSL_READ_WOULD_BLOCK;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(read)");
      break;
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
    case SSL_ERROR_WANT_CLIENT_HELLO_CB:
      event = SSL_READ_WOULD_BLOCK;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_client_hello_cb);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(read/client hello cb)");
      break;
#endif
    case SSL_ERROR_WANT_X509_LOOKUP:
      event = SSL_READ_WOULD_BLOCK;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(read/x509 lookup)");
      break;
    case SSL_ERROR_SYSCALL:
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      if (nread != 0) {
        // not EOF
        event = SSL_READ_ERROR;
        ret   = errno;
        Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
      } else {
        // then EOF observed, treat it as EOS
        // Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, EOF observed violating SSL protocol");
        event = SSL_READ_EOS;
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      event = SSL_READ_EOS;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      event = SSL_READ_ERROR;
      ret   = errno;
      SSL_CLR_ERR_INCR_DYN_STAT(sslvc, ssl_error_ssl, "[SSL_NetVConnection::ssl_read_from_net]: errno=%d", errno);
    } break;
    } // switch
  }   // while

  if (bytes_read > 0) {
    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read=%" PRId64, bytes_read);

    s->vio.ndone += bytes_read;
    sslvc->netActivity(lthread);

    ret = bytes_read;

    // If we read it all, don't worry about the other events and just send read complete
    event = (s->vio.ntodo() <= 0) ? SSL_READ_COMPLETE : SSL_READ_READY;
    if (sslErr == SSL_ERROR_NONE && s->vio.ntodo() > 0) {
      // We stopped with data on the wire (to avoid overbuffering).  Make sure we are triggered
      sslvc->read.triggered = 1;
    }
  } else { // if( bytes_read > 0 )
#if defined(_DEBUG)
    if (bytes_read == 0) {
      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read == 0");
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

    r = socketManager.read(this->con.fd, buffer, buf_len);
    NET_INCREMENT_DYN_STAT(net_calls_to_read_stat);
    total_read += rattempted;

    Debug("ssl", "read_raw_data r=%" PRId64 " rattempted=%" PRId64 " total_read=%" PRId64 " fd=%d", r, rattempted, total_read,
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
  NET_SUM_DYN_STAT(net_read_bytes_stat, r);

  IpMap *pp_ipmap;
  pp_ipmap = SSLConfigParams::proxy_protocol_ipmap;

  if (this->get_is_proxy_protocol()) {
    Debug("proxyprotocol", "[SSLNetVConnection::read_raw_data] proxy protocol is enabled on this port");
    if (pp_ipmap->count() > 0) {
      Debug("proxyprotocol",
            "[SSLNetVConnection::read_raw_data] proxy protocol has a configured whitelist of trusted IPs - checking");

      // At this point, using get_remote_addr() will return the ip of the
      // proxy source IP, not the Proxy Protocol client ip. Since we are
      // checking the ip of the actual source of this connection, this is
      // what we want now.
      void *payload = nullptr;
      if (!pp_ipmap->contains(get_remote_addr(), &payload)) {
        Debug("proxyprotocol",
              "[SSLNetVConnection::read_raw_data] proxy protocol src IP is NOT in the configured whitelist of trusted IPs - "
              "closing connection");
        r = -ENOTCONN; // Need a quick close/exit here to refuse the connection!!!!!!!!!
        goto proxy_protocol_bypass;
      } else {
        char new_host[INET6_ADDRSTRLEN];
        Debug("proxyprotocol", "[SSLNetVConnection::read_raw_data] Source IP [%s] is in the trusted whitelist for proxy protocol",
              ats_ip_ntop(this->get_remote_addr(), new_host, sizeof(new_host)));
      }
    } else {
      Debug("proxyprotocol",
            "[SSLNetVConnection::read_raw_data] proxy protocol DOES NOT have a configured whitelist of trusted IPs but "
            "proxy protocol is enabled on this port - processing all connections");
    }

    if (ssl_has_proxy_v1(this, buffer, &r)) {
      Debug("proxyprotocol", "[SSLNetVConnection::read_raw_data] ssl has proxy_v1 header");
      set_remote_addr(get_proxy_protocol_src_addr());
    } else {
      Debug("proxyprotocol",
            "[SSLNetVConnection::read_raw_data] proxy protocol was enabled, but required header was not present in the "
            "transaction - closing connection");
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

  Debug("ssl", "%p read r=%" PRId64 " total=%" PRId64 " bio=%d\n", this, r, total_read, this->handShakeBioStored);

  // check for errors
  if (r <= 0) {
    if (r == -EAGAIN || r == -ENOTCONN) {
      NET_INCREMENT_DYN_STAT(net_calls_to_read_nodata_stat);
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
  if (BIO_eof(SSL_get_rbio(this->ssl))) {
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
      BIO *rbio = BIO_new_fd(this->get_socket(), BIO_NOCLOSE);
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
    readSignalError(nh, (int)r);
    Debug("ssl", "[SSLNetVConnection::net_read_io] client renegotiation setting read signal error");
    return;
  }

  // If it is not enabled, lower its priority.  This allows
  // a fast connection to speed match a slower connection by
  // shifting down in priority even if it could read.
  if (!s->enabled || s->vio.op != VIO::READ) {
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
          this->sslHandshakeStatus = SSL_HANDSHAKE_DONE;

          // Copy over all data already read in during the SSL_accept
          // (the client hello message)
          NetState *s            = &this->read;
          MIOBufferAccessor &buf = s->vio.buffer;
          int64_t r              = buf.writer()->write(this->handShakeHolder);
          s->vio.nbytes += r;
          s->vio.ndone += r;

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
      ink_assert(this->handShakeReader != nullptr);
      if (SSLConfigParams::ssl_handshake_timeout_in > 0) {
        double handshake_time = ((double)(Thread::get_hrtime() - sslHandshakeBeginTime) / 1000000000);
        Debug("ssl", "ssl handshake for vc %p, took %.3f seconds, configured handshake_timer: %d", this, handshake_time,
              SSLConfigParams::ssl_handshake_timeout_in);
        if (handshake_time > SSLConfigParams::ssl_handshake_timeout_in) {
          Debug("ssl", "ssl handshake for vc %p, expired, release the connection", this);
          read.triggered = 0;
          nh->read_ready_list.remove(this);
          readSignalError(nh, VC_EVENT_EOS);
          return;
        }
      }
      // move over to the socket if we haven't already
      if (this->handShakeBuffer) {
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
      Debug("ssl", "ssl handshake EVENT_DONE ntodo=%" PRId64, ntodo);
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
  if (ntodo <= 0 || !buf.writer()->write_avail()) {
    read_disable(nh, this);
    return;
  }

  // At this point we are at the post-handshake SSL processing
  //
  // not sure if this do-while loop is really needed here, please replace
  // this comment if you know
  do {
    ret = ssl_read_from_net(this, lthread, r);
    if (ret == SSL_READ_READY || ret == SSL_READ_ERROR_NONE) {
      bytes += r;
    }
    ink_assert(bytes >= 0);
  } while ((ret == SSL_READ_READY && bytes == 0) || ret == SSL_READ_ERROR_NONE);

  if (bytes > 0) {
    if (ret == SSL_READ_WOULD_BLOCK || ret == SSL_READ_READY) {
      if (readSignalAndUpdate(VC_EVENT_READ_READY) != EVENT_CONT) {
        Debug("ssl", "ssl_read_from_net, readSignal != EVENT_CONT");
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
      Debug("ssl", "ssl_read_from_net, mutex switched");
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
    Debug("ssl", "read_from_net, read finished - would block");
#if TS_USE_PORT
    if (ret == SSL_READ_WOULD_BLOCK) {
      readReschedule(nh);
    } else {
      writeReschedule(nh);
    }
#endif
    break;

  case SSL_READ_EOS:
    // close the connection if we have SSL_READ_EOS, this is the return value from ssl_read_from_net() if we get an
    // SSL_ERROR_ZERO_RETURN from SSL_get_error()
    // SSL_ERROR_ZERO_RETURN means that the origin server closed the SSL connection
    read.triggered = 0;
    readSignalDone(VC_EVENT_EOS, nh);

    if (bytes > 0) {
      Debug("ssl", "read_from_net, read finished - EOS");
    } else {
      Debug("ssl", "read_from_net, read finished - 0 useful bytes read, bytes used by SSL layer");
    }
    break;
  case SSL_READ_COMPLETE:
    readSignalDone(VC_EVENT_READ_COMPLETE, nh);
    Debug("ssl", "read_from_net, read finished - signal done");
    break;
  case SSL_READ_ERROR:
    this->read.triggered = 0;
    readSignalError(nh, (int)r);
    Debug("ssl", "read_from_net, read finished - read error");
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
    now                       = Thread::get_hrtime_updated();
    int msec_since_last_write = ink_hrtime_diff_msec(now, sslLastWriteTime);

    if (msec_since_last_write > SSL_DEF_TLS_RECORD_MSEC_THRESHOLD) {
      // reset sslTotalBytesSent upon inactivity for SSL_DEF_TLS_RECORD_MSEC_THRESHOLD
      sslTotalBytesSent = 0;
    }
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, now %" PRId64 ",lastwrite %" PRId64 " ,msec_since_last_write %d", now,
          sslLastWriteTime, msec_since_last_write);
  }

  if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == this->attributes) {
    return this->super::load_buffer_and_write(towrite, buf, total_written, needs);
  }

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
          SSL_INCREMENT_DYN_STAT(ssl_total_dyn_def_tls_record_count);
        } else {
          dynamic_tls_record_size = SSL_MAX_TLS_RECORD_SIZE;
          SSL_INCREMENT_DYN_STAT(ssl_total_dyn_max_tls_record_count);
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
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, before SSLWriteBuffer, l=%" PRId64 ", towrite=%" PRId64 ", b=%p", l,
          towrite, current_block);
    err = SSLWriteBuffer(ssl, current_block, l, num_really_written);

    // We wrote all that we thought we should
    if (num_really_written > 0) {
      total_written += num_really_written;
      buf.reader()->consume(num_really_written);
    }

    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite,Number of bytes written=%" PRId64 " , total=%" PRId64 "",
          num_really_written, total_written);
    NET_INCREMENT_DYN_STAT(net_calls_to_write_stat);
  } while (num_really_written == try_to_write && total_written < towrite);

  if (total_written > 0) {
    sslLastWriteTime = now;
    sslTotalBytesSent += total_written;
  }
  redoWriteSize = 0;
  if (num_really_written > 0) {
    needs |= EVENTIO_WRITE;
  } else {
    switch (err) {
    case SSL_ERROR_NONE:
      Debug("ssl", "SSL_write-SSL_ERROR_NONE");
      break;
    case SSL_ERROR_WANT_READ:
      needs |= EVENTIO_READ;
      num_really_written = -EAGAIN;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_READ");
      break;
    case SSL_ERROR_WANT_WRITE:
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
    case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
    case SSL_ERROR_WANT_X509_LOOKUP: {
      if (SSL_ERROR_WANT_WRITE == err) {
        SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
        redoWriteSize = l;
      } else if (SSL_ERROR_WANT_X509_LOOKUP == err) {
        SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
      }
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
      else if (SSL_ERROR_WANT_CLIENT_HELLO_CB == err) {
        SSL_INCREMENT_DYN_STAT(ssl_error_want_client_hello_cb);
      }
#endif
      needs |= EVENTIO_WRITE;
      num_really_written = -EAGAIN;
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    }
    case SSL_ERROR_SYSCALL:
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      Debug("ssl.error", "SSL_write-SSL_ERROR_SYSCALL");
      break;
    // end of stream
    case SSL_ERROR_ZERO_RETURN:
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      // Treat SSL_ERROR_SSL as EPIPE error.
      num_really_written = -EPIPE;
      SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSL_write-SSL_ERROR_SSL errno=%d", errno);
    } break;
    }
  }
  return num_really_written;
}

SSLNetVConnection::SSLNetVConnection() {}

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
      Debug("ssl-shutdown", "previous shutdown state 0x%x", shutdown_mode);
      int new_shutdown_mode = shutdown_mode | SSL_RECEIVED_SHUTDOWN;

      if (new_shutdown_mode != shutdown_mode) {
        // We do not need to sit around and wait for the client's close-notify if
        // they have not already sent it.  We will still be standards compliant
        Debug("ssl-shutdown", "new SSL_set_shutdown 0x%x", new_shutdown_mode);
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
        Debug("ssl-shutdown", "SSL_shutdown %s", (ret) ? "success" : "failed");
      }
    }
  }
  // Go on and do the unix socket cleanups
  super::do_io_close(lerrno);
}

void
SSLNetVConnection::clear()
{
  if (ssl != nullptr) {
    SSL_free(ssl);
    ssl = nullptr;
  }

  sslHandshakeStatus          = SSL_HANDSHAKE_ONGOING;
  sslHandshakeBeginTime       = 0;
  sslLastWriteTime            = 0;
  sslTotalBytesSent           = 0;
  sslClientRenegotiationAbort = false;
  sslSessionCacheHit          = false;

  curHook         = nullptr;
  hookOpRequested = SSL_HOOK_OP_DEFAULT;
  npnSet          = nullptr;
  npnEndpoint     = nullptr;
  free_handshake_buffers();
  sslTrace = false;

  super::clear();
}
void
SSLNetVConnection::free(EThread *t)
{
  ink_release_assert(t == this_ethread());

  // cancel OOB
  cancel_OOB();
  // close socket fd
  if (con.fd != NO_FD) {
    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, -1);
  }
  con.close();

  ats_free(tunnel_host);

  clear();
  SET_CONTINUATION_HANDLER(this, (SSLNetVConnHandler)&SSLNetVConnection::startEvent);
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
    Debug("ssl", "Stopping handshake due to server shutting down.");
    return EVENT_ERROR;
  }
  if (sslHandshakeBeginTime == 0) {
    sslHandshakeBeginTime = Thread::get_hrtime();
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
        Debug("ssl", "Failed to get dest ip, errno = [%d]", errno);
        return EVENT_ERROR;
      }
      SSLCertContext *cc = lookup->find(dst);
      if (is_debug_tag_set("ssl")) {
        IpEndpoint src;
        ip_port_text_buffer ipb1, ipb2;
        int ip_len = sizeof(src);

        if (0 != safe_getpeername(this->get_socket(), &src.sa, &ip_len)) {
          Debug("ssl", "Failed to get src ip, errno = [%d]", errno);
          return EVENT_ERROR;
        }
        ats_ip_nptop(&dst, ipb1, sizeof(ipb1));
        ats_ip_nptop(&src, ipb2, sizeof(ipb2));
        Debug("ssl", "IP context is %p for [%s] -> [%s], default context %p", cc, ipb2, ipb1, lookup->defaultContext());
      }

      // Escape if this is marked to be a tunnel.
      // No data has been read at this point, so we can go
      // directly into blind tunnel mode

      if (cc && SSLCertContextOption::OPT_TUNNEL == cc->opt) {
        if (this->is_transparent) {
          this->attributes   = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
          sslHandshakeStatus = SSL_HANDSHAKE_DONE;
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
      this->ssl = make_ssl_connection(lookup->defaultContext(), this);
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
      auto nps                 = sniParam->getPropertyConfig(serverKey);
      shared_SSL_CTX sharedCTX = nullptr;
      SSL_CTX *clientCTX       = nullptr;

      // First Look to see if there are override parameters
      if (options.ssl_client_cert_name) {
        std::string certFilePath = Layout::get()->relative_to(params->clientCertPathOnly, options.ssl_client_cert_name);
        std::string keyFilePath;
        if (options.ssl_client_private_key_name) {
          keyFilePath = Layout::get()->relative_to(params->clientKeyPathOnly, options.ssl_client_private_key_name);
        }
        std::string caCertFilePath;
        if (options.ssl_client_ca_cert_name) {
          caCertFilePath = Layout::get()->relative_to(params->clientCACertPath, options.ssl_client_ca_cert_name);
        }
        sharedCTX =
          params->getCTX(certFilePath.c_str(), keyFilePath.empty() ? nullptr : keyFilePath.c_str(),
                         caCertFilePath.empty() ? params->clientCACertFilename : caCertFilePath.c_str(), params->clientCACertPath);
      } else if (options.ssl_client_ca_cert_name) {
        std::string caCertFilePath = Layout::get()->relative_to(params->clientCACertPath, options.ssl_client_ca_cert_name);
        sharedCTX = params->getCTX(params->clientCertPath, params->clientKeyPath, caCertFilePath.c_str(), params->clientCACertPath);
      } else if (nps && !nps->client_cert_file.empty()) {
        // If no overrides available, try the available nextHopProperty by reading from context mappings
        sharedCTX =
          params->getCTX(nps->client_cert_file.c_str(), nps->client_key_file.empty() ? nullptr : nps->client_key_file.c_str(),
                         params->clientCACertFilename, params->clientCACertPath);
      } else { // Just stay with the values passed down from the SM for verify
        clientCTX = params->client_ctx.get();
      }

      if (sharedCTX) {
        clientCTX = sharedCTX.get();
      }

      if (options.verifyServerPolicy != YamlSNIConfig::Policy::UNSET) {
        // Stay with conf-override version as the highest priority
      } else if (nps && nps->verifyServerPolicy != YamlSNIConfig::Policy::UNSET) {
        options.verifyServerPolicy = nps->verifyServerPolicy;
      } else {
        options.verifyServerPolicy = params->verifyServerPolicy;
      }

      if (options.verifyServerProperties != YamlSNIConfig::Property::UNSET) {
        // Stay with conf-override version as the highest priority
      } else if (nps && nps->verifyServerProperties != YamlSNIConfig::Property::UNSET) {
        options.verifyServerProperties = nps->verifyServerProperties;
      } else {
        options.verifyServerProperties = params->verifyServerProperties;
      }

      if (!clientCTX) {
        SSLErrorVC(this, "failed to create SSL client session");
        return EVENT_ERROR;
      }

      this->ssl = make_ssl_connection(clientCTX, this);
      if (this->ssl == nullptr) {
        SSLErrorVC(this, "failed to create SSL client session");
        return EVENT_ERROR;
      }

      SSL_set_verify(this->ssl, SSL_VERIFY_PEER, verify_callback);

      if (this->options.sni_servername) {
        if (SSL_set_tlsext_host_name(this->ssl, this->options.sni_servername)) {
          Debug("ssl", "using SNI name '%s' for client handshake", this->options.sni_servername.get());
        } else {
          Debug("ssl.error", "failed to set SNI name '%s' for client handshake", this->options.sni_servername.get());
          SSL_INCREMENT_DYN_STAT(ssl_sni_name_set_failure);
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
    if (!curHook) {
      Debug("ssl", "Initialize preaccept curHook from NULL");
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_VCONN_START_HOOK));
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
    sslHandshakeStatus = SSL_HANDSHAKE_DONE;
    return EVENT_DONE;
  }

  Debug("ssl", "Go on with the handshake state=%d", sslHandshakeHookState);

  // All the pre-accept hooks have completed, proceed with the actual accept.
  if (this->handShakeReader) {
    if (BIO_eof(SSL_get_rbio(this->ssl))) { // No more data in the buffer
      // Is this the first read?
      if (!this->handShakeReader->is_read_avail_more_than(0) && !this->handShakeHolder->is_read_avail_more_than(0)) {
        Debug("ssl", "%p first read\n", this);
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

#if TS_USE_TLS_ASYNC
  if (SSLConfigParams::async_handshake_enabled) {
    SSL_set_mode(ssl, SSL_MODE_ASYNC);
  }
#endif
  ssl_error_t ssl_error = SSLAccept(ssl);
#if TS_USE_TLS_ASYNC
  if (ssl_error == SSL_ERROR_WANT_ASYNC) {
    size_t numfds;
    OSSL_ASYNC_FD waitfd;
    // Set up the epoll entry for the signalling
    if (SSL_get_all_async_fds(ssl, &waitfd, &numfds) && numfds > 0) {
      // Temporarily disable regular net
      read_disable(nh, this);
      this->ep.stop(); // Modify used in read_disable doesn't work for edge triggered epol
      // Have to have the read NetState enabled because we are using it for the signal vc
      read.enabled = true;
      write_disable(nh, this);
      PollDescriptor *pd = get_PollDescriptor(this_ethread());
      this->ep.start(pd, waitfd, this, EVENTIO_READ);
      this->ep.type = EVENTIO_READWRITE_VC;
    }
  } else if (SSLConfigParams::async_handshake_enabled) {
    // Clean up the epoll entry for signalling
    SSL_clear_mode(ssl, SSL_MODE_ASYNC);
    this->ep.stop();
    // Reactivate the socket, ready to rock
    PollDescriptor *pd = get_PollDescriptor(this_ethread());
    this->ep.start(
      pd, this,
      EVENTIO_READ |
        EVENTIO_WRITE); // Again we must muck with the eventloop directly because of limits with these methods and edge trigger
    if (ssl_error == SSL_ERROR_WANT_READ) {
      this->reenable(&read.vio);
      this->read.triggered = 1;
    }
  }
#endif
  if (ssl_error != SSL_ERROR_NONE) {
    err = errno;
    SSLVCDebug(this, "SSL handshake error: %s (%d), errno=%d", SSLErrorName(ssl_error), ssl_error, err);

    // start a blind tunnel if tr-pass is set and data does not look like ClientHello
    char *buf = handShakeBuffer ? handShakeBuffer->buf() : nullptr;
    if (getTransparentPassThrough() && buf && *buf != SSL_OP_HANDSHAKE) {
      SSLVCDebug(this, "Data does not look like SSL handshake, starting blind tunnel");
      this->attributes   = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      sslHandshakeStatus = SSL_HANDSHAKE_ONGOING;
      return EVENT_CONT;
    }
  }

  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (is_debug_tag_set("ssl")) {
      X509 *cert = SSL_get_peer_certificate(ssl);

      Debug("ssl", "SSL server handshake completed successfully");
      if (cert) {
        debug_certificate_name("client certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("client certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }

    sslHandshakeStatus = SSL_HANDSHAKE_DONE;

    if (sslHandshakeBeginTime) {
      sslHandshakeEndTime                 = Thread::get_hrtime();
      const ink_hrtime ssl_handshake_time = sslHandshakeEndTime - sslHandshakeBeginTime;

      Debug("ssl", "ssl handshake time:%" PRId64, ssl_handshake_time);
      SSL_INCREMENT_DYN_STAT_EX(ssl_total_handshake_time_stat, ssl_handshake_time);
      SSL_INCREMENT_DYN_STAT(ssl_total_success_handshake_count_in_stat);
    }

    {
      const unsigned char *proto = nullptr;
      unsigned len               = 0;

      // If it's possible to negotiate both NPN and ALPN, then ALPN
      // is preferred since it is the server's preference.  The server
      // preference would not be meaningful if we let the client
      // preference have priority.
      SSL_get0_alpn_selected(ssl, &proto, &len);
      if (len == 0) {
        SSL_get0_next_proto_negotiated(ssl, &proto, &len);
      }

      if (len) {
        // If there's no NPN set, we should not have done this negotiation.
        ink_assert(this->npnSet != nullptr);

        this->npnEndpoint = this->npnSet->findEndpoint(proto, len);
        this->npnSet      = nullptr;

        if (this->npnEndpoint == nullptr) {
          Error("failed to find registered SSL endpoint for '%.*s'", (int)len, (const char *)proto);
          return EVENT_ERROR;
        }

        Debug("ssl", "client selected next protocol '%.*s'", len, proto);
      } else {
        Debug("ssl", "client did not select a next protocol");
      }
    }

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
      sslHandshakeStatus = SSL_HANDSHAKE_ONGOING;
      return EVENT_CONT;
    } else {
      //  Stopping for some other reason, perhaps loading certificate
      return SSL_WAIT_FOR_HOOK;
    }
#endif

#if TS_USE_TLS_ASYNC
  case SSL_ERROR_WANT_ASYNC:
    return SSL_WAIT_FOR_ASYNC;
#endif

  case SSL_ERROR_WANT_ACCEPT:
    return EVENT_CONT;

  case SSL_ERROR_SSL: {
    SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSLNetVConnection::sslServerHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
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

  ink_assert(SSLNetVCAccess(ssl) == this);

  // Initialize properly for a client connection
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_PRE) {
    sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE;
  }

  // Do outbound hook processing here
  // Continue on if we are in the invoked state.  The hook has not yet reenabled
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE) {
    return SSL_WAIT_FOR_HOOK;
  }

  // Go do the preaccept hooks
  if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE) {
    if (!curHook) {
      Debug("ssl", "Initialize outbound connect curHook from NULL");
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_START_HOOK));
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

  ssl_error = SSLConnect(ssl);
  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (is_debug_tag_set("ssl")) {
      X509 *cert = SSL_get_peer_certificate(ssl);

      Debug("ssl", "SSL client handshake completed successfully");

      if (cert) {
        debug_certificate_name("server certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("server certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }

    // if the handshake is complete and write is enabled reschedule the write
    if (closed == 0 && write.enabled) {
      writeReschedule(nh);
    }

    SSL_INCREMENT_DYN_STAT(ssl_total_success_handshake_count_out_stat);

    sslHandshakeStatus = SSL_HANDSHAKE_DONE;
    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_WRITE");
    SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_READ");
    return SSL_HANDSHAKE_WANT_READ;
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
  case SSL_ERROR_WANT_CLIENT_HELLO_CB:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_client_hello_cb);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_CLIENT_HELLO_CB");
    break;
#endif
  case SSL_ERROR_WANT_X509_LOOKUP:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_X509_LOOKUP");
    break;

  case SSL_ERROR_WANT_ACCEPT:
    return SSL_HANDSHAKE_WANT_ACCEPT;

  case SSL_ERROR_WANT_CONNECT:
    break;

  case SSL_ERROR_ZERO_RETURN:
    SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, EOS");
    return EVENT_ERROR;

  case SSL_ERROR_SYSCALL:
    err = errno;
    SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, syscall");
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SSL:
  default: {
    err = (errno) ? errno : -ENET_CONNECT_FAILED;
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    // FIXME -- This triggers a retry on cases of cert validation errors....
    SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL");
    if (e) {
      if (this->options.sni_servername) {
        Debug("ssl.error", "SSL connection failed for '%s': %s", this->options.sni_servername.get(), buf);
      } else {
        char buff[INET6_ADDRSTRLEN];
        ats_ip_ntop(this->get_remote_addr(), buff, INET6_ADDRSTRLEN);
        Debug("ssl.error", "SSL connection failed for '%s': %s", buff, buf);
      }
    }
    return EVENT_ERROR;
  } break;
  }
  return EVENT_CONT;
}

void
SSLNetVConnection::registerNextProtocolSet(SSLNextProtocolSet *s)
{
  this->npnSet = s;
}

// NextProtocolNegotiation TLS extension callback. The NPN extension
// allows the client to select a preferred protocol, so all we have
// to do here is tell them what out protocol set is.
int
SSLNetVConnection::advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned int *outlen, void * /*arg ATS_UNUSED */)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  ink_release_assert(netvc != nullptr);
  if (netvc->npnSet && netvc->npnSet->advertiseProtocols(out, outlen)) {
    // Successful return tells OpenSSL to advertise.
    return SSL_TLSEXT_ERR_OK;
  }

  return SSL_TLSEXT_ERR_NOACK;
}

// ALPN TLS extension callback. Given the client's set of offered
// protocols, we have to select a protocol to use for this session.
int
SSLNetVConnection::select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                                        const unsigned char *in ATS_UNUSED, unsigned inlen ATS_UNUSED, void *)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  const unsigned char *npn = nullptr;
  unsigned npnsz           = 0;

  ink_release_assert(netvc != nullptr);
  if (netvc->npnSet && netvc->npnSet->advertiseProtocols(&npn, &npnsz)) {
    // SSL_select_next_proto chooses the first server-offered protocol that appears in the clients protocol set, ie. the
    // server selects the protocol. This is a n^2 search, so it's preferable to keep the protocol set short.
    if (SSL_select_next_proto((unsigned char **)out, outlen, npn, npnsz, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
      Debug("ssl", "selected ALPN protocol %.*s", (int)(*outlen), *out);
      return SSL_TLSEXT_ERR_OK;
    }
  }

  *out    = nullptr;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}

void
SSLNetVConnection::reenable(NetHandler *nh, int event)
{
  Debug("ssl", "Handshake reenable from state=%d", sslHandshakeHookState);

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
    if (event == TS_EVENT_ERROR) {
      sslHandshakeStatus = SSL_HANDSHAKE_ERROR;
    }
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
    Debug("ssl", "iterate from reenable curHook=%p", curHook);
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
      Debug("ssl", "Reenable preaccept");
      sslHandshakeHookState = HANDSHAKE_HOOKS_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_START, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_OUTBOUND_PRE) {
      Debug("ssl", "Reenable outbound connect");
      sslHandshakeHookState = HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
      ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_START, this);
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_DONE) {
      if (this->get_context() == NET_VCONNECTION_OUT) {
        ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_CLOSE, this);
      } else {
        ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_CLOSE, this);
      }
    } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_VERIFY_SERVER) {
      Debug("ssl", "ServerVerify");
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
    Debug("ssl", "iterate from reenable curHook=%p %d", curHook, sslHandshakeHookState);
  }
  this->readReschedule(nh);
}

bool
SSLNetVConnection::sslContextSet(void *ctx)
{
  bool zret = true;
  if (ssl) {
    SSL_set_SSL_CTX(ssl, static_cast<SSL_CTX *>(ctx));
  } else {
    zret = false;
  }
  return zret;
}

bool
SSLNetVConnection::callHooks(TSEvent eventId)
{
  // Only dealing with the SNI/CERT hook so far.
  ink_assert(eventId == TS_EVENT_SSL_CLIENT_HELLO || eventId == TS_EVENT_SSL_CERT || eventId == TS_EVENT_SSL_SERVERNAME ||
             eventId == TS_EVENT_SSL_VERIFY_SERVER || eventId == TS_EVENT_SSL_VERIFY_CLIENT || eventId == TS_EVENT_VCONN_CLOSE ||
             eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE);
  Debug("ssl", "callHooks sslHandshakeHookState=%d eventID=%d", this->sslHandshakeHookState, eventId);

  // Move state if it is appropriate
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
    } else if (eventId == TS_EVENT_VCONN_CLOSE) {
      // Jump to the end
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    }
    break;
  case HANDSHAKE_HOOKS_SNI:
    if (eventId == TS_EVENT_SSL_CERT) {
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
    } else if (eventId == TS_EVENT_VCONN_CLOSE) {
      // Jump to the end
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    }
    break;
  default:
    break;
  }

  // Look for hooks associated with the event
  switch (this->sslHandshakeHookState) {
  case HANDSHAKE_HOOKS_CLIENT_HELLO:
  case HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    if (!curHook) {
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_CLIENT_HELLO_HOOK));
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
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_VERIFY_SERVER_HOOK));
    } else {
      curHook = curHook->next();
    }
    break;
  case HANDSHAKE_HOOKS_SNI:
    if (!curHook) {
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_SERVERNAME_HOOK));
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
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_CERT_HOOK));
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
      curHook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_VERIFY_CLIENT_HOOK));
    } else {
      curHook = curHook->next();
    }
  // fallthrough
  case HANDSHAKE_HOOKS_DONE:
  case HANDSHAKE_HOOKS_OUTBOUND_PRE:
    if (eventId == TS_EVENT_VCONN_CLOSE) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = ssl_hooks->get(TSSslHookInternalID(TS_VCONN_CLOSE_HOOK));
      } else {
        curHook = curHook->next();
      }
    } else if (eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE) {
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = ssl_hooks->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_CLOSE_HOOK));
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

  Debug("ssl", "callHooks iterated to curHook=%p", curHook);

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
    curHook->invoke(eventId, this);
    reenabled =
      (this->sslHandshakeHookState != HANDSHAKE_HOOKS_CERT_INVOKE && this->sslHandshakeHookState != HANDSHAKE_HOOKS_PRE_INVOKE &&
       this->sslHandshakeHookState != HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE);
    Debug("ssl", "Called hook on state=%d reenabled=%d", sslHandshakeHookState, reenabled);
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
  this->ssl = (SSL *)arg;
  // Maybe bring over the stats?

  sslHandshakeStatus = SSL_HANDSHAKE_DONE;
  SSLNetVCAttach(this->ssl, this);
  return EVENT_DONE;
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
    results[retval] = map_tls_protocol_to_tag(getSSLProtocol());
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
  std::string_view tag = map_tls_protocol_to_tag(getSSLProtocol());
  if (prefix.size() <= tag.size() && strncmp(tag.data(), prefix.data(), prefix.size()) == 0) {
    retval = tag.data();
  } else {
    retval = super::protocol_contains(prefix);
  }
  return retval;
}
