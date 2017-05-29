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
#include "ts/ink_config.h"
#include "ts/EventNotify.h"
#include "records/I_RecHttp.h"
#include "P_Net.h"
#include "P_SSLNextProtocolSet.h"
#include "P_SSLUtils.h"
#include "InkAPIInternal.h" // Added to include the ssl_hook definitions
#include "P_SSLConfig.h"
#include "BIO_fastopen.h"
#include "Log.h"
#include "P_SSLClientUtils.h"

#include <climits>
#include <string>

#if !TS_USE_SET_RBIO
// Defined in SSLInternal.c, should probably make a separate include
// file for this at some point
void SSL_set0_rbio(SSL *ssl, BIO *rbio);
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
    MUTEX_TRY_LOCK(lock, target->mutex, eth);
    if (lock.is_locked()) {
      target->handleEvent(eventId, edata);
    } else {
      eventProcessor.schedule_imm(new ContWrapper(mutex, target, eventId, edata), ET_NET);
    }
  }

private:
  Continuation *_target; ///< Continuation to invoke.
  int _eventId;          ///< with this event
  void *_edata;          ///< and this data
};
}

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
  int64_t nread          = 0;

  bool trace = sslvc->getSSLTrace();

  bytes_read = 0;
  while (sslErr == SSL_ERROR_NONE) {
    int64_t block_write_avail = buf.writer()->block_write_avail();
    if (block_write_avail <= 0) {
      buf.writer()->add_block();
      block_write_avail = buf.writer()->block_write_avail();
      if (block_write_avail <= 0) {
        Warning("Cannot add new block");
        break;
      }
    }

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] b->write_avail()=%" PRId64, block_write_avail);
    char *current_block = buf.writer()->end();
    sslErr              = SSLReadBuffer(sslvc->ssl, current_block, block_write_avail, nread);

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] nread=%d", (int)nread);
    if (!sslvc->origin_trace) {
      TraceIn((0 < nread && trace), sslvc->get_remote_addr(), sslvc->get_remote_port(), "WIRE TRACE\tbytes=%d\n%.*s", (int)nread,
              (int)nread, current_block);
    } else {
      char origin_trace_ip[INET6_ADDRSTRLEN];
      ats_ip_ntop(sslvc->origin_trace_addr, origin_trace_ip, sizeof(origin_trace_ip));
      TraceIn((0 < nread && trace), sslvc->get_remote_addr(), sslvc->get_remote_port(), "CLIENT %s:%d\ttbytes=%d\n%.*s",
              origin_trace_ip, sslvc->origin_trace_port, (int)nread, (int)nread, current_block);
    }

    switch (sslErr) {
    case SSL_ERROR_NONE:

#if DEBUG
      SSLDebugBufferPrint("ssl_buff", current_block, nread, "SSL Read");
#endif
      ink_assert(nread);
      bytes_read += nread;
      if (nread > 0) {
        buf.writer()->fill(nread); // Tell the buffer, we've used the bytes
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
    case SSL_ERROR_WANT_X509_LOOKUP:
      TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "Want X509 lookup");
      event = SSL_READ_WOULD_BLOCK;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(read/x509 lookup)");
      break;
    case SSL_ERROR_SYSCALL:
      TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "Syscall Error: %s", strerror(errno));
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      if (nread != 0) {
        // not EOF
        event = SSL_READ_ERROR;
        ret   = errno;
        Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
        TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "Underlying IO error: %d", errno);
      } else {
        // then EOF observed, treat it as EOS
        // Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, EOF observed violating SSL protocol");
        TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "EOF observed violating SSL protocol");
        event = SSL_READ_EOS;
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "Connection closed by peer");
      event = SSL_READ_EOS;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      TraceIn(trace, sslvc->get_remote_addr(), sslvc->get_remote_port(), "SSL Error: sslErr=%d, ERR_get_error=%ld (%s) errno=%d",
              sslErr, e, buf, errno);
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

    event = (s->vio.ntodo() <= 0) ? SSL_READ_COMPLETE : SSL_READ_READY;
  } else { // if( bytes_read > 0 )
#if defined(_DEBUG)
    if (bytes_read == 0) {
      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read == 0");
    }
#endif
  }
  return event;
}

/**
 * Read from socket directly for handshake data.  Store the data in
 * a MIOBuffer.  Place the data in the read BIO so the openssl library
 * has access to it.
 * If for some ready we much abort out of the handshake, we can replay
 * the stored data (e.g. back out to blind tunneling)
 */
int64_t
SSLNetVConnection::read_raw_data()
{
  int64_t r      = 0;
  int64_t toread = INT_MAX;

  // read data
  int64_t rattempted = 0, total_read = 0;
  unsigned niov = 0;
  IOVec tiovec[NET_MAX_IOV];
  if (toread) {
    IOBufferBlock *b = this->handShakeBuffer->first_write_block();
    do {
      niov       = 0;
      rattempted = 0;
      while (b && niov < NET_MAX_IOV) {
        int64_t a = b->write_avail();
        if (a > 0) {
          tiovec[niov].iov_base = b->_end;
          int64_t togo          = toread - total_read - rattempted;
          if (a > togo) {
            a = togo;
          }
          tiovec[niov].iov_len = a;
          rattempted += a;
          niov++;
          if (a >= togo) {
            break;
          }
        }
        b = b->next.get();
      }

      // If there was no room to write into the buffer then skip the read
      if (niov > 0) {
        ink_assert(niov > 0);
        ink_assert(niov <= countof(tiovec));
        r = socketManager.readv(this->con.fd, &tiovec[0], niov);

        NET_INCREMENT_DYN_STAT(net_calls_to_read_stat);
        total_read += rattempted;
      } else { // No more space to write, break out
        r = 0;
        break;
      }
    } while (rattempted && r == rattempted && total_read < toread);

    // if we have already moved some bytes successfully, summarize in r
    if (total_read != rattempted) {
      if (r <= 0) {
        r = total_read - rattempted;
      } else {
        r = total_read - rattempted + r;
      }
    }
    // check for errors
    if (r <= 0) {
      if (r == -EAGAIN || r == -ENOTCONN) {
        NET_INCREMENT_DYN_STAT(net_calls_to_read_nodata_stat);
      }
      return r;
    }
    NET_SUM_DYN_STAT(net_read_bytes_stat, r);

    this->handShakeBuffer->fill(r);
  }

  char *start              = this->handShakeReader->start();
  char *end                = this->handShakeReader->end();
  this->handShakeBioStored = end - start;

  // Sets up the buffer as a read only bio target
  // Must be reset on each read
  BIO *rbio = BIO_new_mem_buf(start, this->handShakeBioStored);
  BIO_set_mem_eof_return(rbio, -1);
  SSL_set0_rbio(this->ssl, rbio);

  return r;
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

  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, lthread, s->vio._cont);
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
    int err;

    if (get_context() == NET_VCONNECTION_OUT) {
      ret = sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = sslStartHandShake(SSL_EVENT_SERVER, err);
    }
    // If we have flipped to blind tunnel, don't read ahead
    if (this->handShakeReader && this->attributes != HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
      // Check and consume data that has been read
      if (BIO_eof(SSL_get_rbio(this->ssl))) {
        this->handShakeReader->consume(this->handShakeBioStored);
        this->handShakeBioStored = 0;
      }
    } else if (this->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
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
        this->sslHandShakeComplete = true;

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
      return;
    }
    if (ret == EVENT_ERROR) {
      this->read.triggered = 0;
      readSignalError(nh, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT) {
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
      read.triggered = 0;
      nh->read_ready_list.remove(this);
      readReschedule(nh);
    } else if (ret == SSL_HANDSHAKE_WANT_CONNECT || ret == SSL_HANDSHAKE_WANT_WRITE) {
      write.triggered = 0;
      nh->write_ready_list.remove(this);
      writeReschedule(nh);
    } else if (ret == EVENT_DONE) {
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
    } else if (ret == SSL_WAIT_FOR_HOOK) {
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
  // If the read BIO is not already a socket, consider changing it
  if (this->handShakeReader) {
    // Check out if there is anything left in the current bio
    if (!BIO_eof(SSL_get_rbio(this->ssl))) {
      // Still data remaining in the current BIO block
    } else {
      // Consume what SSL has read so far.
      this->handShakeReader->consume(this->handShakeBioStored);

      // If we are empty now, switch over
      if (this->handShakeReader->read_avail() <= 0) {
        // Switch the read bio over to a socket bio
        SSL_set_rfd(this->ssl, this->get_socket());
        this->free_handshake_buffers();
      } else {
        // Setup the next iobuffer block to drain
        char *start              = this->handShakeReader->start();
        char *end                = this->handShakeReader->end();
        this->handShakeBioStored = end - start;

        // Sets up the buffer as a read only bio target
        // Must be reset on each read
        BIO *rbio = BIO_new_mem_buf(start, this->handShakeBioStored);
        BIO_set_mem_eof_return(rbio, -1);
        SSL_set0_rbio(this->ssl, rbio);
      }
    }
  }
  // Otherwise, we already replaced the buffer bio with a socket bio

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
#ifdef TS_USE_PORT
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

  bool trace = getSSLTrace();

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

    if (!l) {
      break;
    }

    try_to_write       = l;
    num_really_written = 0;
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, before SSLWriteBuffer, l=%" PRId64 ", towrite=%" PRId64 ", b=%p", l,
          towrite, current_block);
    err = SSLWriteBuffer(ssl, current_block, l, num_really_written);

    if (!origin_trace) {
      TraceOut((0 < num_really_written && trace), get_remote_addr(), get_remote_port(), "WIRE TRACE\tbytes=%d\n%.*s",
               (int)num_really_written, (int)num_really_written, current_block);
    } else {
      char origin_trace_ip[INET6_ADDRSTRLEN];
      ats_ip_ntop(origin_trace_addr, origin_trace_ip, sizeof(origin_trace_ip));
      TraceOut((0 < num_really_written && trace), get_remote_addr(), get_remote_port(), "CLIENT %s:%d\ttbytes=%d\n%.*s",
               origin_trace_ip, origin_trace_port, (int)num_really_written, (int)num_really_written, current_block);
    }

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
    case SSL_ERROR_WANT_X509_LOOKUP: {
      if (SSL_ERROR_WANT_WRITE == err) {
        SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
      } else if (SSL_ERROR_WANT_X509_LOOKUP == err) {
        SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
        TraceOut(trace, get_remote_addr(), get_remote_port(), "Want X509 lookup");
      }

      needs |= EVENTIO_WRITE;
      num_really_written = -EAGAIN;
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    }
    case SSL_ERROR_SYSCALL:
      TraceOut(trace, get_remote_addr(), get_remote_port(), "Syscall Error: %s", strerror(errno));
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      Debug("ssl.error", "SSL_write-SSL_ERROR_SYSCALL");
      break;
    // end of stream
    case SSL_ERROR_ZERO_RETURN:
      TraceOut(trace, get_remote_addr(), get_remote_port(), "SSL Error: zero return");
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL Error: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", err, e, buf,
              errno);
      num_really_written = -errno;
      SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSL_write-SSL_ERROR_SSL errno=%d", errno);
    } break;
    }
  }
  return num_really_written;
}

SSLNetVConnection::SSLNetVConnection()
{
}

void
SSLNetVConnection::do_io_close(int lerrno)
{
  if (this->ssl != nullptr && sslHandShakeComplete) {
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
    if (x != 0) {
      // Send the close-notify
      int ret = SSL_shutdown(ssl);
      Debug("ssl-shutdown", "SSL_shutdown %s", (ret) ? "success" : "failed");
    }
  }
  // Go on and do the unix socket cleanups
  super::do_io_close(lerrno);
}

void
SSLNetVConnection::free(EThread *t)
{
  got_remote_addr = false;
  got_local_addr  = false;
  read.vio.mutex.clear();
  write.vio.mutex.clear();
  this->mutex.clear();
  action_.mutex.clear();
  this->ep.stop();
  this->con.close();
  flags = 0;

  SET_CONTINUATION_HANDLER(this, (SSLNetVConnHandler)&SSLNetVConnection::startEvent);

  if (nh) {
    nh->read_ready_list.remove(this);
    nh->write_ready_list.remove(this);
    nh = nullptr;
  }

  read.triggered      = 0;
  write.triggered     = 0;
  read.enabled        = 0;
  write.enabled       = 0;
  read.vio._cont      = nullptr;
  write.vio._cont     = nullptr;
  read.vio.vc_server  = nullptr;
  write.vio.vc_server = nullptr;

  closed = 0;
  options.reset();
  con.close();

  ink_assert(con.fd == NO_FD);

  if (ssl != nullptr) {
    SSL_free(ssl);
    ssl = nullptr;
  }

  sslHandShakeComplete        = false;
  sslHandshakeBeginTime       = 0;
  sslLastWriteTime            = 0;
  sslTotalBytesSent           = 0;
  sslClientRenegotiationAbort = false;
  sslSessionCacheHit          = false;

  if (SSL_HOOKS_ACTIVE == sslPreAcceptHookState) {
    Error("SSLNetVconnection freed with outstanding hook");
  }

  sslPreAcceptHookState = SSL_HOOKS_INIT;
  curHook               = nullptr;
  hookOpRequested       = SSL_HOOK_OP_DEFAULT;
  npnSet                = nullptr;
  npnEndpoint           = nullptr;
  sslHandShakeComplete  = false;
  free_handshake_buffers();
  sslTrace = false;

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
      IpEndpoint ip;
      int namelen = sizeof(ip);
      safe_getsockname(this->get_socket(), &ip.sa, &namelen);
      SSLCertContext *cc = lookup->find(ip);
      if (is_debug_tag_set("ssl")) {
        IpEndpoint src, dst;
        ip_port_text_buffer ipb1, ipb2;
        int ip_len;

        safe_getsockname(this->get_socket(), &dst.sa, &(ip_len = sizeof ip));
        safe_getpeername(this->get_socket(), &src.sa, &(ip_len = sizeof ip));
        ats_ip_nptop(&dst, ipb1, sizeof(ipb1));
        ats_ip_nptop(&src, ipb2, sizeof(ipb2));
        Debug("ssl", "IP context is %p for [%s] -> [%s], default context %p", cc, ipb2, ipb1, lookup->defaultContext());
      }

      // Escape if this is marked to be a tunnel.
      // No data has been read at this point, so we can go
      // directly into blind tunnel mode
      if (cc && SSLCertContext::OPT_TUNNEL == cc->opt && this->is_transparent) {
        this->attributes     = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
        sslHandShakeComplete = true;
        SSL_free(this->ssl);
        this->ssl = nullptr;
        return EVENT_DONE;
      }

      // Attach the default SSL_CTX to this SSL session. The default context is never going to be able
      // to negotiate a SSL session, but it's enough to trampoline us into the SNI callback where we
      // can select the right server certificate.
      this->ssl = make_ssl_connection(lookup->defaultContext(), this);

#if !(TS_USE_TLS_SNI)
      // set SSL trace
      if (SSLConfigParams::ssl_wire_trace_enabled) {
        bool trace = computeSSLTrace();
        Debug("ssl", "sslnetvc. setting trace to=%s", trace ? "true" : "false");
        setSSLTrace(trace);
      }
#endif
    }

    if (this->ssl == nullptr) {
      SSLErrorVC(this, "failed to create SSL server session");
      return EVENT_ERROR;
    }

    return sslServerHandShakeEvent(err);

  case SSL_EVENT_CLIENT:
    if (this->ssl == nullptr) {
      SSL_CTX *clientCTX = nullptr;
      if (this->options.clientCertificate) {
        const char *certfile = (const char *)this->options.clientCertificate;
        if (certfile != nullptr) {
          clientCTX = params->getCTX(certfile);
          if (clientCTX != nullptr) {
            Debug("ssl", "context for %s is found at %p", this->options.clientCertificate.get(), (void *)clientCTX);
          } else {
            Debug("ssl", "failed to find context for %s", this->options.clientCertificate.get());
          }
        }
      } else {
        clientCTX = params->client_ctx;
      }
      this->ssl = make_ssl_connection(clientCTX, this);
      if (this->ssl != nullptr) {
        uint8_t clientVerify = this->options.clientVerificationFlag;
        int verifyValue      = clientVerify & 1 ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
        SSL_set_verify(this->ssl, verifyValue, verify_callback);
      }

      if (this->ssl == nullptr) {
        SSLErrorVC(this, "failed to create SSL client session");
        return EVENT_ERROR;
      }

#if TS_USE_TLS_SNI
      if (this->options.sni_servername) {
        if (SSL_set_tlsext_host_name(this->ssl, this->options.sni_servername)) {
          Debug("ssl", "using SNI name '%s' for client handshake", this->options.sni_servername.get());
        } else {
          Debug("ssl.error", "failed to set SNI name '%s' for client handshake", this->options.sni_servername.get());
          SSL_INCREMENT_DYN_STAT(ssl_sni_name_set_failure);
        }
      }
#endif
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
  if (SSL_HOOKS_DONE != sslPreAcceptHookState) {
    // Get the first hook if we haven't started invoking yet.
    if (SSL_HOOKS_INIT == sslPreAcceptHookState) {
      curHook               = ssl_hooks->get(TS_VCONN_PRE_ACCEPT_INTERNAL_HOOK);
      sslPreAcceptHookState = SSL_HOOKS_INVOKE;
    } else if (SSL_HOOKS_INVOKE == sslPreAcceptHookState) {
      // if the state is anything else, we haven't finished
      // the previous hook yet.
      curHook = curHook->next();
    }

    if (SSL_HOOKS_INVOKE == sslPreAcceptHookState) {
      if (nullptr == curHook) { // no hooks left, we're done
        sslPreAcceptHookState = SSL_HOOKS_DONE;
      } else {
        sslPreAcceptHookState = SSL_HOOKS_ACTIVE;
        ContWrapper::wrap(nh->mutex.get(), curHook->m_cont, TS_EVENT_VCONN_PRE_ACCEPT, this);
        return SSL_WAIT_FOR_HOOK;
      }
    } else { // waiting for hook to complete
             /* A note on waiting for the hook. I believe that because this logic
                cannot proceed as long as a hook is outstanding, the underlying VC
                can't go stale. If that can happen for some reason, we'll need to be
                more clever and provide some sort of cancel mechanism. I have a trap
                in SSLNetVConnection::free to check for this.
             */
      return SSL_WAIT_FOR_HOOK;
    }
  }

  // handle SNI Hooks after PreAccept Hooks
  if (HANDSHAKE_HOOKS_DONE != sslHandshakeHookState && HANDSHAKE_HOOKS_PRE != sslHandshakeHookState) {
    return SSL_WAIT_FOR_HOOK;
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
    sslHandShakeComplete = true;
    return EVENT_DONE;
  }

  int retval = 1; // Initialze with a non-error value

  // All the pre-accept hooks have completed, proceed with the actual accept.
  if (BIO_eof(SSL_get_rbio(this->ssl))) { // No more data in the buffer
    // Read from socket to fill in the BIO buffer with the
    // raw handshake data before calling the ssl accept calls.
    retval = this->read_raw_data();
    if (retval == 0) {
      // EOF, go away, we stopped in the handshake
      SSLDebugVC(this, "SSL handshake error: EOF");
      return EVENT_ERROR;
    }
  }

  ssl_error_t ssl_error = SSLAccept(ssl);
  bool trace            = getSSLTrace();

  if (ssl_error != SSL_ERROR_NONE) {
    err = errno;
    SSLDebugVC(this, "SSL handshake error: %s (%d), errno=%d", SSLErrorName(ssl_error), ssl_error, err);

    // start a blind tunnel if tr-pass is set and data does not look like ClientHello
    char *buf = handShakeBuffer->buf();
    if (getTransparentPassThrough() && buf && *buf != SSL_OP_HANDSHAKE) {
      SSLDebugVC(this, "Data does not look like SSL handshake, starting blind tunnel");
      this->attributes     = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      sslHandShakeComplete = false;
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

    sslHandShakeComplete = true;

    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake completed successfully");
    // do we want to include cert info in trace?

    if (sslHandshakeBeginTime) {
      const ink_hrtime ssl_handshake_time = Thread::get_hrtime() - sslHandshakeBeginTime;
      Debug("ssl", "ssl handshake time:%" PRId64, ssl_handshake_time);
      sslHandshakeBeginTime = 0;
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

#if TS_USE_TLS_ALPN
      SSL_get0_alpn_selected(ssl, &proto, &len);
#endif /* TS_USE_TLS_ALPN */

#if TS_USE_TLS_NPN
      if (len == 0) {
        SSL_get0_next_proto_negotiated(ssl, &proto, &len);
      }
#endif /* TS_USE_TLS_NPN */

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
        TraceIn(trace, get_remote_addr(), get_remote_port(), "client selected next protocol'%.*s'", len, proto);
      } else {
        Debug("ssl", "client did not select a next protocol");
        TraceIn(trace, get_remote_addr(), get_remote_port(), "client did not select a next protocol");
      }
    }

    return EVENT_DONE;

  case SSL_ERROR_WANT_CONNECT:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_CONNECT");
    return SSL_HANDSHAKE_WANT_CONNECT;

  case SSL_ERROR_WANT_WRITE:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_WRITE");
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_READ");
    if (retval == -EAGAIN) {
      // No data at the moment, hang tight
      SSLDebugVC(this, "SSL handshake: EAGAIN");
      return SSL_HANDSHAKE_WANT_READ;
    } else if (retval < 0) {
      // An error, make us go away
      SSLDebugVC(this, "SSL handshake error: read_retval=%d", retval);
      return EVENT_ERROR;
    }
    return SSL_HANDSHAKE_WANT_READ;

// This value is only defined in openssl has been patched to
// enable the sni callback to break out of the SSL_accept processing
#ifdef SSL_ERROR_WANT_SNI_RESOLVE
  case SSL_ERROR_WANT_X509_LOOKUP:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_X509_LOOKUP");
    return EVENT_CONT;
  case SSL_ERROR_WANT_SNI_RESOLVE:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_SNI_RESOLVE");
#elif SSL_ERROR_WANT_X509_LOOKUP
  case SSL_ERROR_WANT_X509_LOOKUP:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_X509_LOOKUP");
#endif
#if defined(SSL_ERROR_WANT_SNI_RESOLVE) || defined(SSL_ERROR_WANT_X509_LOOKUP)
    if (this->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL || SSL_HOOK_OP_TUNNEL == hookOpRequested) {
      this->attributes     = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      sslHandShakeComplete = false;
      return EVENT_CONT;
    } else {
      //  Stopping for some other reason, perhaps loading certificate
      return SSL_WAIT_FOR_HOOK;
    }
#endif

  case SSL_ERROR_WANT_ACCEPT:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_WANT_ACCEPT");
    return EVENT_CONT;

  case SSL_ERROR_SSL: {
    SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSLNetVConnection::sslServerHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    TraceIn(trace, get_remote_addr(), get_remote_port(),
            "SSL server handshake ERROR_SSL: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", ssl_error, e, buf, errno);
    return EVENT_ERROR;
  }

  case SSL_ERROR_ZERO_RETURN:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_ZERO_RETURN");
    return EVENT_ERROR;
  case SSL_ERROR_SYSCALL:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_SYSCALL");
    return EVENT_ERROR;
  default:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL server handshake ERROR_OTHER");
    return EVENT_ERROR;
  }
}

int
SSLNetVConnection::sslClientHandShakeEvent(int &err)
{
  bool trace = getSSLTrace();
  ssl_error_t ssl_error;

  ink_assert(SSLNetVCAccess(ssl) == this);

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

    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake completed successfully");
    // do we want to include cert info in trace?

    sslHandShakeComplete = true;
    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_WRITE");
    SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake ERROR_WANT_WRITE");
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_READ");
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake ERROR_WANT_READ");
    return SSL_HANDSHAKE_WANT_READ;

  case SSL_ERROR_WANT_X509_LOOKUP:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_X509_LOOKUP");
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake ERROR_WANT_X509_LOOKUP");
    break;

  case SSL_ERROR_WANT_ACCEPT:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake ERROR_WANT_ACCEPT");
    return SSL_HANDSHAKE_WANT_ACCEPT;

  case SSL_ERROR_WANT_CONNECT:
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake ERROR_WANT_CONNECT");
    break;

  case SSL_ERROR_ZERO_RETURN:
    SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, EOS");
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake EOS");
    return EVENT_ERROR;

  case SSL_ERROR_SYSCALL:
    err = errno;
    SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, syscall");
    TraceIn(trace, get_remote_addr(), get_remote_port(), "SSL client handshake Syscall Error: %s", strerror(errno));
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SSL:
  default: {
    err = (errno) ? errno : -ENET_CONNECT_FAILED;
    // FIXME -- This triggers a retry on cases of cert validation errors....
    Debug("ssl", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL");
    SSL_CLR_ERR_INCR_DYN_STAT(this, ssl_error_ssl, "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL");
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    TraceIn(trace, get_remote_addr(), get_remote_port(),
            "SSL client handshake ERROR_SSL: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", ssl_error, e, buf, errno);
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

#if HAVE_SSL_SELECT_NEXT_PROTO
    if (SSL_select_next_proto((unsigned char **)out, outlen, npn, npnsz, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
      Debug("ssl", "selected ALPN protocol %.*s", (int)(*outlen), *out);
      return SSL_TLSEXT_ERR_OK;
    }
#endif /* HAVE_SSL_SELECT_NEXT_PROTO */
  }

  *out    = nullptr;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}

void
SSLNetVConnection::reenable(NetHandler *nh)
{
  if (sslPreAcceptHookState != SSL_HOOKS_DONE) {
    sslPreAcceptHookState = SSL_HOOKS_INVOKE;
  } else if (sslHandshakeHookState == HANDSHAKE_HOOKS_INVOKE) {
    // Reenabling from the handshake callback
    //
    // Originally, we would wait for the callback to go again to execute additinonal
    // hooks, but since the callbacks are associated with the context and the context
    // can be replaced by the plugin, it didn't seem reasonable to assume that the
    // callback would be executed again.  So we walk through the rest of the hooks
    // here in the reenable.
    if (curHook != nullptr) {
      curHook = curHook->next();
    }
    if (curHook != nullptr) {
      // Invoke the hook and return, wait for next reenable
      curHook->invoke(TS_EVENT_SSL_CERT, this);
      return;
    } else { // curHook == nullptr
      // empty, set state to HOOKS_DONE
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    }
  }
  this->readReschedule(nh);
}

bool
SSLNetVConnection::sslContextSet(void *ctx)
{
#if TS_USE_TLS_SNI
  bool zret = true;
  if (ssl) {
    SSL_set_SSL_CTX(ssl, static_cast<SSL_CTX *>(ctx));
  } else {
    zret = false;
  }
#else
  bool zret      = false;
#endif
  return zret;
}

bool
SSLNetVConnection::callHooks(TSEvent eventId)
{
  // Only dealing with the SNI/CERT hook so far.
  ink_assert(eventId == TS_EVENT_SSL_CERT || eventId == TS_EVENT_SSL_SERVERNAME);
  Debug("ssl", "callHooks sslHandshakeHookState=%d", this->sslHandshakeHookState);

  // First time through, set the type of the hook that is currently being invoked
  if ((this->sslHandshakeHookState == HANDSHAKE_HOOKS_PRE || this->sslHandshakeHookState == HANDSHAKE_HOOKS_DONE) &&
      eventId == TS_EVENT_SSL_CERT) {
    // the previous hook should be DONE and set curHook to nullptr before trigger the sni hook.
    ink_assert(curHook == nullptr);
    // set to HOOKS_CERT means CERT/SNI hooks has called by SSL_accept()
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
    // get Hooks
    curHook = ssl_hooks->get(TS_SSL_CERT_INTERNAL_HOOK);
  } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
    if (!curHook) {
      curHook = ssl_hooks->get(TS_SSL_SERVERNAME_INTERNAL_HOOK);
    }
  } else {
    // Not in the right state
    // reenable and continue
    return true;
  }

  bool reenabled = true;
  if (curHook != nullptr) {
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_INVOKE;
    curHook->invoke(eventId, this);
    reenabled = eventId != TS_EVENT_SSL_CERT || (this->sslHandshakeHookState != HANDSHAKE_HOOKS_INVOKE);
  }

  // All done with the current hook chain
  if (curHook == nullptr) {
    if (eventId == TS_EVENT_SSL_CERT) {
      // Set the HookState to done because we are all done with the CERT/SERVERNAME hook chains
      sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
      // Reset the HookState to PRE, so the cert hook chain can start
      sslHandshakeHookState = HANDSHAKE_HOOKS_PRE;
    }
  }
  return reenabled;
}

bool
SSLNetVConnection::computeSSLTrace()
{
// this has to happen before the handshake or else sni_servername will be nullptr
#if TS_USE_TLS_SNI
  bool sni_trace;
  if (ssl) {
    const char *ssl_servername   = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    char *wire_trace_server_name = SSLConfigParams::ssl_wire_trace_server_name;
    Debug("ssl", "for wiretrace, ssl_servername=%s, wire_trace_server_name=%s", ssl_servername, wire_trace_server_name);
    sni_trace = ssl_servername && wire_trace_server_name && (0 == strcmp(wire_trace_server_name, ssl_servername));
  } else {
    sni_trace = false;
  }
#else
  bool sni_trace = false;
#endif

  // count based on ip only if they set an IP value
  const sockaddr *remote_addr = get_remote_addr();
  bool ip_trace               = false;
  if (SSLConfigParams::ssl_wire_trace_ip) {
    ip_trace = (*SSLConfigParams::ssl_wire_trace_ip == remote_addr);
  }

  // count based on percentage
  int percentage = SSLConfigParams::ssl_wire_trace_percentage;
  int random;
  bool trace;

  // we only generate random numbers as needed (to maintain correct percentage)
  if (SSLConfigParams::ssl_wire_trace_server_name && SSLConfigParams::ssl_wire_trace_ip) {
    random = this_ethread()->generator.random() % 100; // range [0-99]
    trace  = sni_trace && ip_trace && (percentage > random);
  } else if (SSLConfigParams::ssl_wire_trace_server_name) {
    random = this_ethread()->generator.random() % 100; // range [0-99]
    trace  = sni_trace && (percentage > random);
  } else if (SSLConfigParams::ssl_wire_trace_ip) {
    random = this_ethread()->generator.random() % 100; // range [0-99]
    trace  = ip_trace && (percentage > random);
  } else {
    random = this_ethread()->generator.random() % 100; // range [0-99]
    trace  = percentage > random;
  }

  Debug("ssl", "ssl_netvc random=%d, trace=%s", random, trace ? "TRUE" : "FALSE");

  return trace;
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

  this->sslHandShakeComplete = true;
  SSLNetVCAttach(this->ssl, this);
  return EVENT_DONE;
}

ts::StringView
SSLNetVConnection::map_tls_protocol_to_tag(const char *proto_string) const
{
  // Prefix for the string the SSL library hands back.
  static const ts::StringView PREFIX("TLSv1", ts::StringView::literal);

  ts::StringView retval;
  ts::StringView proto(proto_string);

  if (PREFIX.isNoCasePrefixOf(proto)) {
    proto += PREFIX.size(); // skip the prefix part.
    if (proto.size() <= 0) {
      retval = IP_PROTO_TAG_TLS_1_0;
    } else if (*proto == '.') {
      ++proto; // skip .
      if (proto.size() == 1) {
        if (*proto == '1') {
          retval = IP_PROTO_TAG_TLS_1_1;
        } else if (*proto == '2') {
          retval = IP_PROTO_TAG_TLS_1_2;
        } else if (*proto == '3') {
          retval = IP_PROTO_TAG_TLS_1_3;
        }
      }
    }
  }
  return retval;
}

int
SSLNetVConnection::populate_protocol(ts::StringView *results, int n) const
{
  int retval = 0;
  if (n > retval) {
    results[retval] = map_tls_protocol_to_tag(getSSLProtocol());
    if (results[retval]) {
      ++retval;
    }
    if (n > retval) {
      retval += super::populate_protocol(results + retval, n - retval);
    }
  }
  return retval;
}

const char *
SSLNetVConnection::protocol_contains(ts::StringView prefix) const
{
  const char *retval = nullptr;
  ts::StringView tag = map_tls_protocol_to_tag(getSSLProtocol());
  if (prefix.size() <= tag.size() && strncmp(tag.ptr(), prefix.ptr(), prefix.size()) == 0) {
    retval = tag.ptr();
  } else {
    retval = super::protocol_contains(prefix);
  }
  return retval;
}
