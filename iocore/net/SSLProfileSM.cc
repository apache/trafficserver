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
#include "ts/ink_platform.h"
#include "ts/InkErrno.h"
#include "P_SSLUtils.h"
#include "InkAPIInternal.h" // Added to include the ssl_hook definitions
#include "P_SSLConfig.h"
#include "Log.h"

#include <climits>
#include <string>

#if !TS_USE_SET_RBIO
// Defined in SSLInternal.c, should probably make a separate include
// file for this at some point
void SSL_set_rbio(SSL *ssl, BIO *rbio);
#endif

// This is missing from BoringSSL
#ifndef BIO_eof
#define BIO_eof(b) (int)BIO_ctrl(b, BIO_CTRL_EOF, 0, NULL)
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

ClassAllocator<SSLProfileSM> sslProfileSMAllocator("sslProfileSMAllocator");

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
              void *edata = 0                ///< Data for invocation of @a target.
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
       void *edata = 0                ///< Data for invocation of @a target.
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

SSL *
SSLProfileSM::make_ssl_connection(SSL_CTX *ctx)
{
  if (likely(ssl = SSL_new(ctx))) {
    // Only set up the bio stuff for the server side
    if (vc->get_context() == NET_VCONNECTION_OUT) {
      SSL_set_fd(ssl, vc->get_socket());
    } else {
      initialize_handshake_buffers();
      BIO *rbio = BIO_new(BIO_s_mem());
      BIO *wbio = BIO_new_fd(vc->get_socket(), BIO_NOCLOSE);
      BIO_set_mem_eof_return(wbio, -1);
      SSL_set_bio(ssl, rbio, wbio);
    }

    SSLProfileSMAttach(ssl, this);
  }

  return ssl;
}

static void
debug_certificate_name(const char *msg, X509_NAME *name)
{
  BIO *bio;

  if (name == NULL) {
    return;
  }

  bio = BIO_new(BIO_s_mem());
  if (bio == NULL) {
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

int64_t
SSLProfileSM::read(void *buf, int64_t len, int &err)
{
  int64_t r  = 0;
  bool trace = this->getTrace();
  err        = SSLReadBuffer(ssl, buf, len, r);
  if (r > 0) {
    if (!vc->getOriginTrace()) {
      TraceIn((trace), vc->get_remote_addr(), vc->get_remote_port(), "WIRE TRACE\tbytes=%d\n%.*s", (int)r, (int)r, (char *)buf);
    } else {
      char origin_trace_ip[INET6_ADDRSTRLEN];
      ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));
      TraceIn((trace), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
              vc->getOriginTraceAddr()->port(), (int)r, (int)r, (char *)buf);
    }
  }
  return r;
}

int64_t
SSLProfileSM::write(void *buf, int64_t len, int &err)
{
  int64_t r  = 0;
  bool trace = this->getTrace();
  err        = SSLWriteBuffer(ssl, buf, len, r);
  if (r > 0) {
    if (!vc->getOriginTrace()) {
      TraceOut((trace), vc->get_remote_addr(), vc->get_remote_port(), "WIRE TRACE\tbytes=%d\n%.*s", (int)r, (int)r, (char *)buf);
    } else {
      char origin_trace_ip[INET6_ADDRSTRLEN];
      ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));
      TraceOut((trace), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
               vc->getOriginTraceAddr()->port(), (int)r, (int)r, (char *)buf);
    }
  }
  return r;
}

// reference from SSLNetVConnection::net_read_io()
int64_t
SSLProfileSM::read_from_net(int64_t toread, int64_t &rattempted, int64_t &total_read, MIOBufferAccessor &buf)
{
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);
  int event                 = SSL_READ_ERROR_NONE;
  ssl_error_t sslErr        = SSL_ERROR_NONE;
  int64_t nread             = 0;

  // referenced from SSLNetVConnection::net_read_io()
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
        SSL_set_rfd(this->ssl, vc->get_socket());
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
        SSL_set_rbio(this->ssl, rbio);
      }
    }
  }
  // Otherwise, we already replaced the buffer bio with a socket bio

  // referenced from ssl_read_from_net()
  bool trace = this->getTrace();

  rattempted = 0; // non used
  total_read = 0;
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

    Debug("ssl", "[SSLProfileSM::read_from_net] b->write_avail()=%" PRId64, block_write_avail);
    char *current_block = buf.writer()->end();
    nread               = this->read(current_block, block_write_avail, sslErr);

    Debug("ssl", "[SSLNetProfileSM::read_from_net] nread=%d", (int)nread);

    switch (sslErr) {
    case SSL_ERROR_NONE:

#if DEBUG
      SSLDebugBufferPrint("ssl_buff", current_block, nread, "SSL Read");
#endif
      ink_assert(nread);
      total_read += nread;
      if (nread > 0) {
        buf.writer()->fill(nread); // Tell the buffer, we've used the bytes
      }
      break;
    case SSL_ERROR_WANT_WRITE:
      // event = SSL_WRITE_WOULD_BLOCK;
      event = -EAGAIN;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
      Debug("ssl.error", "[SSLProfileSM::read_from_net] SSL_ERROR_WOULD_BLOCK(write)");
      break;
    case SSL_ERROR_WANT_READ:
      // event = SSL_READ_WOULD_BLOCK;
      event = -EAGAIN;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
      Debug("ssl.error", "[SSLProfileSM::read_from_net] SSL_ERROR_WOULD_BLOCK(read)");
      break;
    case SSL_ERROR_WANT_X509_LOOKUP:
      TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "Want X509 lookup");
      // event = SSL_READ_WOULD_BLOCK;
      event = -EAGAIN;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
      Debug("ssl.error", "[SSLProfileSM::read_from_net] SSL_ERROR_WOULD_BLOCK(read/x509 lookup)");
      break;
    case SSL_ERROR_SYSCALL:
      TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "Syscall Error: %s", strerror(errno));
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      if (nread != 0) {
        // not EOF
        // event = SSL_READ_ERROR;
        event = -errno;
        // ret   = errno;
        Debug("ssl.error", "[SSLProfileSM::read_from_net] SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
        TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "Underlying IO error: %d", errno);
      } else {
        // then EOF observed, treat it as EOS
        TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "EOF observed violating SSL protocol");
        // event = SSL_READ_EOS;
        event = 0;
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "Connection closed by peer");
      // event = SSL_READ_EOS;
      event = 0;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "[SSLProfileSM::read_from_net] SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL Error: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", sslErr,
              e, buf, errno);
      // event = SSL_READ_ERROR;
      event = -errno;
      // ret   = errno;
      SSL_CLR_ERR_INCR_DYN_STAT(netvc, ssl_error_ssl, "[SSLProfileSM::read_from_net]: errno=%d", errno);
    } break;
    } // switch
  }   // while

  if (total_read > 0) {
    Debug("ssl", "[SSLProfileSM::read_from_net] total_read=%" PRId64, total_read);
    event = toread;
  } else {
#if defined(_DEBUG)
    if (total_read == 0) {
      Debug("ssl", "[SSLProfileSM::read_from_net] total_read == 0");
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
SSLProfileSM::read_raw_data()
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
          if (a > togo)
            a                  = togo;
          tiovec[niov].iov_len = a;
          rattempted += a;
          niov++;
          if (a >= togo)
            break;
        }
        b = b->next.get();
      }

      ink_assert(niov > 0);
      ink_assert(niov <= countof(tiovec));
      r = this->raw_readv(&tiovec[0], niov);

      NET_INCREMENT_DYN_STAT(net_calls_to_read_stat);
      total_read += rattempted;
    } while (rattempted && r == rattempted && total_read < toread);

    // if we have already moved some bytes successfully, summarize in r
    if (total_read != rattempted) {
      if (r <= 0)
        r = total_read - rattempted;
      else
        r = total_read - rattempted + r;
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
  SSL_set_rbio(this->ssl, rbio);

  return r;
}

int64_t
SSLProfileSM::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
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
    Debug("ssl", "[SSLProfileSM::load_buffer_and_write] now %" PRId64 ", lastwrite %" PRId64 ", msec_since_last_write %d", now,
          sslLastWriteTime, msec_since_last_write);
  }

  bool trace = getTrace();

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
    Debug("ssl", "SSLProfileSM::loadBufferAndCallWrite, before SSLWriteBuffer, l=%" PRId64 ", towrite=%" PRId64 ", b=%p", l,
          towrite, current_block);
    num_really_written = this->write(current_block, l, err);

    // We wrote all that we thought we should
    if (num_really_written > 0) {
      total_written += num_really_written;
      buf.reader()->consume(num_really_written);
    }

    Debug("ssl", "SSLProfileSM::loadBufferAndCallWrite,Number of bytes written=%" PRId64 " , total=%" PRId64 "", num_really_written,
          total_written);
    NET_INCREMENT_DYN_STAT(net_calls_to_write_stat);
  } while (num_really_written == try_to_write && total_written < towrite);

  if (total_written > 0) {
    sslLastWriteTime = now;
    sslTotalBytesSent += total_written;
  }
  if (num_really_written > 0) {
    needs |= EVENTIO_WRITE;
    return total_written;
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
        TraceOut(trace, vc->get_remote_addr(), vc->get_remote_port(), "Want X509 lookup");
      }

      needs |= EVENTIO_WRITE;
      num_really_written = -EAGAIN;
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    }
    case SSL_ERROR_SYSCALL:
      TraceOut(trace, vc->get_remote_addr(), vc->get_remote_port(), "Syscall Error: %s", strerror(errno));
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      Debug("ssl.error", "SSL_write-SSL_ERROR_SYSCALL");
      break;
    // end of stream
    case SSL_ERROR_ZERO_RETURN:
      TraceOut(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL Error: zero return");
      num_really_written = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default: {
      char buf[512];
      unsigned long e = ERR_peek_last_error();
      ERR_error_string_n(e, buf, sizeof(buf));
      TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL Error: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", err, e,
              buf, errno);
      num_really_written = -errno;
      SSL_CLR_ERR_INCR_DYN_STAT(vc, ssl_error_ssl, "SSL_write-SSL_ERROR_SSL errno=%d", errno);
    } break;
    }
  }
  return num_really_written;
}

SSLProfileSM::SSLProfileSM()
  : UnixNetProfileSM(NULL),
    sslHandshakeBeginTime(0),
    sslLastWriteTime(0),
    sslTotalBytesSent(0),
    handShakeBuffer(NULL),
    handShakeHolder(NULL),
    handShakeReader(NULL),
    handShakeBioStored(0),
    sslPreAcceptHookState(SSL_HOOKS_INIT),
    sslHandshakeDoneHookState(SSL_HOOKS_INIT),
    sslHandshakeHookState(HANDSHAKE_HOOKS_PRE)
{
  type = PROFILE_SM_SSL;
  SET_HANDLER(&SSLProfileSM::handshakeEvent);
}

int
SSLProfileSM::mainEvent(int event, void *data)
{
  Debug("ssl", "SSLProfileSM::mainEvent event = %d", event);
  NetHandler *nh            = static_cast<NetHandler *>(data);
  EThread *lthread          = nh->trigger_event->ethread;
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);
  NetState *s;

  ink_assert(this->getSSLHandShakeComplete());

  // Get lock first
  switch (event) {
  case IOCORE_EVENTS_READ:
    s = &netvc->read;
    break;
  case IOCORE_EVENTS_WRITE:
    s = &netvc->write;
    break;
  default:
    ink_assert(!"not reach");
    return EVENT_DONE;
  }
  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, lthread, s->vio._cont);
  if (!lock.is_locked() || lock.get_mutex() != s->vio.mutex.get()) {
    switch (event) {
    case IOCORE_EVENTS_READ:
      netvc->readReschedule();
      break;
    case IOCORE_EVENTS_WRITE:
      netvc->writeReschedule();
      break;
    default:
      ink_assert(!"not reach");
    }
    return EVENT_DONE;
  }

  ink_release_assert(HttpProxyPort::TRANSPORT_BLIND_TUNNEL != vc->attributes);

  if (event == IOCORE_EVENTS_READ) {
    // If the key renegotiation failed it's over, just signal the error and finish.
    if (this->sslClientRenegotiationAbort == true) {
      netvc->read.triggered = 0;
      netvc->readSignalError(0);
      Debug("ssl", "[SSLProfileSM::handle_read] client renegotiation setting read signal error");
      return EVENT_DONE;
    }
  }

  // No TRY LOCK in handle_read and handle_write
  switch (event) {
  case IOCORE_EVENTS_READ:
    handle_read(nh, nh->trigger_event->ethread);
    break;
  case IOCORE_EVENTS_WRITE:
    handle_write(nh, nh->trigger_event->ethread);
    break;
  default:
    ink_assert(!"not reach");
    return EVENT_DONE;
  }

  return EVENT_DONE;
}

int
SSLProfileSM::handshakeEvent(int event, void *data)
{
  Debug("ssl", "SSLProfileSM::handshakeEvent event = %d", event);
  NetHandler *nh            = static_cast<NetHandler *>(data);
  EThread *lthread          = nh->trigger_event->ethread;
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);
  NetState *s;

  // Get lock first
  switch (event) {
  case IOCORE_EVENTS_READ:
    s = &netvc->read;
    break;
  case IOCORE_EVENTS_WRITE:
    s = &netvc->write;
    break;
  default:
    ink_assert(!"not reach");
    return EVENT_DONE;
  }
  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, lthread, s->vio._cont);
  if (!lock.is_locked() || lock.get_mutex() != s->vio.mutex.get()) {
    switch (event) {
    case IOCORE_EVENTS_READ:
      netvc->readReschedule();
      break;
    case IOCORE_EVENTS_WRITE:
      netvc->writeReschedule();
      break;
    default:
      ink_assert(!"not reach");
    }
    return EVENT_DONE;
  }

  ink_release_assert(HttpProxyPort::TRANSPORT_BLIND_TUNNEL != vc->attributes);

  if (event == IOCORE_EVENTS_READ) {
    // If the key renegotiation failed it's over, just signal the error and finish.
    if (this->sslClientRenegotiationAbort == true) {
      netvc->read.triggered = 0;
      netvc->readSignalError(0);
      Debug("ssl", "[SSLProfileSM::handshakeEvent] client renegotiation setting read signal error");
      return EVENT_DONE;
    }

    // If it is not enabled, lower its priority. This allows
    // a fast connection to speed match a slower connection by
    // shifting down in priority even if it could read.

    if (!s->enabled || s->vio.op != VIO::READ) {
      netvc->readDisable();
      return EVENT_DONE;
    }
  }

  // No TRY LOCK in handle_handshake
  handle_handshake(event, nh, nh->trigger_event->ethread);
  return EVENT_DONE;
}

void
SSLProfileSM::clear()
{
  close();
  sslHandshakeBeginTime = 0;
  sslLastWriteTime      = 0;
  sslTotalBytesSent     = 0;
  if (SSL_HOOKS_ACTIVE == sslPreAcceptHookState) {
    Error("SSLProfileSM::clear freed with outstanding hook");
  }
  sslPreAcceptHookState = SSL_HOOKS_INIT;
  curHook               = 0;
  free_handshake_buffers();

  SSLM::clear();
  NetProfileSM::clear();
}

void
SSLProfileSM::close()
{
  if (this->ssl != NULL && sslHandShakeComplete) {
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
    ssize_t x = recv(vc->get_socket(), &c, 1, MSG_PEEK);
    // x < 0 means error.  x == 0 means fin sent
    if (x != 0) {
      // Send the close-notify
      int ret = SSL_shutdown(ssl);
      Debug("ssl-shutdown", "SSL_shutdown %s", (ret) ? "success" : "failed");
    }
  }
}

void
SSLProfileSM::free(EThread *t)
{
  clear();

  if (this->globally_allocated) {
    sslProfileSMAllocator.free(this);
  } else {
    THREAD_FREE(this, sslProfileSMAllocator, t);
  }
}

SSLProfileSM *
SSLProfileSM::allocate(EThread *t)
{
  SSLProfileSM *ssl_profile_sm;

  if (t) {
    ssl_profile_sm = THREAD_ALLOC_INIT(sslProfileSMAllocator, t);
  } else {
    if (likely(ssl_profile_sm = sslProfileSMAllocator.alloc()))
      ssl_profile_sm->globally_allocated = true;
  }
  return ssl_profile_sm;
}

// reference from SSLNetVConnection::net_read_io() && write_to_net()
void
SSLProfileSM::handle_handshake(int event, NetHandler *nh, EThread *lthread)
{
  int err, ret;
  UnixNetVConnection *netvc  = static_cast<UnixNetVConnection *>(vc);
  TSSslHookInternalID hookId = TS_SSL_CLIENT_HANDSHAKE_INTERNAL_HOOK;
  TSHttpHookID eventId       = TS_SSL_CLIENT_HANDSHAKE_HOOK;

  if (getSSLHandShakeComplete()) {
    ret = EVENT_DONE;
  } else { // Call ssl StartHandShake
    if (netvc->get_context() == NET_VCONNECTION_OUT) {
      ret = sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = sslStartHandShake(SSL_EVENT_SERVER, err);
    }
  }

  // Check for blind tunnel (only on IOCORE_EVENTS_READ and SSL_EVENT_SERVER)
  if (event == IOCORE_EVENTS_READ) {
    // If we have flipped to blind tunnel, don't read ahead
    if (vc->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
      // If the handshake isn't set yet, this means the tunnel
      // decision was make in the SNI callback.  We must move
      // the client hello message back into the standard read.vio
      // so it will get forwarded onto the origin server
      if (!this->getSSLHandShakeComplete()) {
        this->sslHandShakeComplete = 1;

        // Copy over all data already read in during the SSL_accept
        // (the client hello message)
        NetState *s            = &netvc->read;
        MIOBufferAccessor &buf = s->vio.buffer;
        int64_t r              = buf.writer()->write(this->handShakeHolder);
        s->vio.nbytes += r;
        s->vio.ndone += r;

        // Clean up the handshake buffers
        this->free_handshake_buffers();
      }
      netvc->del_profile_sm(lthread);
      netvc->readSignalDone(VC_EVENT_READ_COMPLETE);
      return;
    }
  }
  if (this->handShakeReader) {
    // Check and consume data that has been read
    if (BIO_eof(SSL_get_rbio(this->ssl))) {
      this->handShakeReader->consume(this->handShakeBioStored);
      this->handShakeBioStored = 0;
    }
  }

  // Check for return value from sslStartHandShake
  switch (ret) {
  case EVENT_ERROR:
    if (event == IOCORE_EVENTS_READ) {
      netvc->read.triggered = 0;
      netvc->readSignalError(err);
    } else if (event == IOCORE_EVENTS_WRITE) {
      netvc->write.triggered = 0;
      netvc->writeSignalError(err);
    }
    break;

  case SSL_HANDSHAKE_WANT_READ:
  case SSL_HANDSHAKE_WANT_ACCEPT:
    if (event == IOCORE_EVENTS_READ && netvc->get_context() == NET_VCONNECTION_IN &&
        SSLConfigParams::ssl_handshake_timeout_in > 0) {
      double handshake_time = ((double)(Thread::get_hrtime() - sslHandshakeBeginTime) / 1000000000);
      Debug("ssl", "ssl handshake for vc %p, took %.3f seconds, configured handshake_timer: %d", this->vc, handshake_time,
            SSLConfigParams::ssl_handshake_timeout_in);
      if (handshake_time > SSLConfigParams::ssl_handshake_timeout_in) {
        Debug("ssl", "ssl handshake for vc %p, expired, release the connection", this->vc);
        netvc->read.triggered = 0;
        nh->read_ready_list.remove(netvc);
        netvc->readSignalError(VC_EVENT_EOS);
        return;
      }
    }
    netvc->read.triggered = 0;
    netvc->readReschedule();
    break;

  case SSL_HANDSHAKE_WANT_WRITE:
  case SSL_HANDSHAKE_WANT_CONNECT:
    netvc->write.triggered = 0;
    netvc->writeReschedule();
    break;

  case EVENT_DONE:
    Debug("ssl", "EVENT_DONE netvc->read.triggered=%d netvc->write.triggered=%d event=%d", netvc->read.triggered,
          netvc->write.triggered, event);
    if (netvc->get_context() == NET_VCONNECTION_IN) {
      hookId  = TS_SSL_SERVER_HANDSHAKE_INTERNAL_HOOK;
      eventId = TS_SSL_SERVER_HANDSHAKE_HOOK;
    }
    if (SSL_HOOKS_DONE != sslHandshakeDoneHookState) {
      // Get the first hook if we haven't started invoking yet.
      if (SSL_HOOKS_INIT == sslHandshakeDoneHookState) {
        curHook                   = ssl_hooks->get(hookId);
        sslHandshakeDoneHookState = SSL_HOOKS_INVOKE;
      } else if (SSL_HOOKS_INVOKE == sslHandshakeDoneHookState) {
        // if the state is anything else, we haven't finished the previous hook yet.
        curHook = curHook->next();
      }
      if (SSL_HOOKS_INVOKE == sslHandshakeDoneHookState) {
        if (0 == curHook) { // no hooks left, we're done
          sslHandshakeDoneHookState = SSL_HOOKS_DONE;
        } else {
          sslHandshakeDoneHookState = SSL_HOOKS_ACTIVE;
          ContWrapper::wrap(mutex.get(), curHook->m_cont, eventId, vc);
          return;
        }
      } else { // waiting for hook to complete
        return;
      }
    }

    SET_HANDLER(&SSLProfileSM::mainEvent);
    if (event == IOCORE_EVENTS_READ) {
      if (endpoint()) { // for ProtocolProbeSessionAccept
        netvc->readSignalDone(VC_EVENT_READ_COMPLETE);
        return;
      }
      netvc->read.triggered = 1;
      netvc->readReschedule();
    } else if (event == IOCORE_EVENTS_WRITE) {
      netvc->write.triggered = 1;
      netvc->writeReschedule();
    }
    break;

  case SSL_WAIT_FOR_HOOK:
    // avoid read & write Reschedule - done when the plugin calls us back to reenable
    return;
    break;

  case EVENT_CONT:
  default:
    if (event == IOCORE_EVENTS_READ) {
      netvc->readReschedule();
    } else if (event == IOCORE_EVENTS_WRITE) {
      netvc->writeReschedule();
    }
    break;
  }
}

int
SSLProfileSM::sslStartHandShake(int event, int &err)
{
  if (sslHandshakeBeginTime == 0) {
    sslHandshakeBeginTime = Thread::get_hrtime();
    // net_activity will not be triggered until after the handshake
    vc->set_inactivity_timeout(HRTIME_SECONDS(SSLConfigParams::ssl_handshake_timeout_in));
  }

  switch (event) {
  case SSL_EVENT_SERVER:
    if (this->ssl == NULL) {
      SSLCertificateConfig::scoped_config lookup;
      IpEndpoint ip;
      int namelen = sizeof(ip);
      safe_getsockname(vc->get_socket(), &ip.sa, &namelen);
      SSLCertContext *cc = lookup->find(ip);
      if (is_debug_tag_set("ssl")) {
        IpEndpoint src, dst;
        ip_port_text_buffer ipb1, ipb2;
        int ip_len;

        safe_getsockname(vc->get_socket(), &dst.sa, &(ip_len = sizeof ip));
        safe_getpeername(vc->get_socket(), &src.sa, &(ip_len = sizeof ip));
        ats_ip_nptop(&dst, ipb1, sizeof(ipb1));
        ats_ip_nptop(&src, ipb2, sizeof(ipb2));
        Debug("ssl", "IP context is %p for [%s] -> [%s], default context %p", cc, ipb2, ipb1, lookup->defaultContext());
      }

      // Escape if this is marked to be a tunnel.
      // No data has been read at this point, so we can go
      // directly into blind tunnel mode
      if (cc && SSLCertContext::OPT_TUNNEL == cc->opt && vc->get_is_transparent()) {
        vc->attributes       = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
        sslHandShakeComplete = 1;
        SSL_free(this->ssl);
        this->ssl = NULL;
        return EVENT_DONE;
      }

      // Attach the default SSL_CTX to this SSL session. The default context is never going to be able
      // to negotiate a SSL session, but it's enough to trampoline us into the SNI callback where we
      // can select the right server certificate.
      this->make_ssl_connection(lookup->defaultContext());

#if !(TS_USE_TLS_SNI)
      // set SSL trace
      if (SSLConfigParams::ssl_wire_trace_enabled) {
        bool trace = computeSSLTrace();
        Debug("ssl", "netvc with SSLProfileSM. setting trace to=%s", trace ? "true" : "false");
        this->setTrace(trace);
      }
#endif
    }

    if (this->ssl == NULL) {
      SSLErrorVC(vc, "failed to create SSL server session");
      return EVENT_ERROR;
    }

    return sslServerHandShakeEvent(err);

  case SSL_EVENT_CLIENT:
    if (this->ssl == NULL && this->make_ssl_connection(ssl_NetProcessor.client_ctx) != NULL) {
#if TS_USE_TLS_SNI
      if (vc->options.sni_servername) {
        if (SSL_set_tlsext_host_name(ssl, vc->options.sni_servername)) {
          Debug("ssl", "using SNI name '%s' for client handshake", vc->options.sni_servername.get());
        } else {
          Debug("ssl.error", "failed to set SNI name '%s' for client handshake", vc->options.sni_servername.get());
          SSL_INCREMENT_DYN_STAT(ssl_sni_name_set_failure);
        }
      }
#endif
    }

    if (this->ssl == NULL) {
      SSLErrorVC(vc, "failed to create SSL client session");
      return EVENT_ERROR;
    }

    return sslClientHandShakeEvent(err);

  default:
    ink_assert(0);
    return EVENT_ERROR;
  }
}

int
SSLProfileSM::sslServerHandShakeEvent(int &err)
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
      if (0 == curHook) { // no hooks left, we're done
        sslPreAcceptHookState = SSL_HOOKS_DONE;
      } else {
        sslPreAcceptHookState = SSL_HOOKS_ACTIVE;
        ContWrapper::wrap(mutex.get(), curHook->m_cont, TS_EVENT_VCONN_PRE_ACCEPT, vc);
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
    vc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    SSL_free(this->ssl);
    this->ssl = NULL;
    // Don't mark the handshake as complete yet,
    // Will be checking for that flag not being set after
    // we get out of this callback, and then will shuffle
    // over the buffered handshake packets to the O.S.
    return EVENT_DONE;
  } else if (SSL_HOOK_OP_TERMINATE == hookOpRequested) {
    sslHandShakeComplete = 1;
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
      SSLDebugVC(vc, "SSL handshake error: EOF");
      return EVENT_ERROR;
    }
  }

  ssl_error_t ssl_error = SSLAccept(ssl);
  bool trace            = this->getTrace();

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

    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake completed successfully");
    // do we want to include cert info in trace?

    if (sslHandshakeBeginTime) {
      const ink_hrtime ssl_handshake_time = Thread::get_hrtime() - sslHandshakeBeginTime;
      Debug("ssl", "ssl handshake time:%" PRId64, ssl_handshake_time);
      sslHandshakeBeginTime = 0;
      SSL_INCREMENT_DYN_STAT_EX(ssl_total_handshake_time_stat, ssl_handshake_time);
      SSL_INCREMENT_DYN_STAT(ssl_total_success_handshake_count_in_stat);
    }

    {
      const unsigned char *proto = NULL;
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
        ink_assert(this->npnSet != NULL);

        this->npnEndpoint = this->npnSet->findEndpoint(proto, len);
        this->npnSet      = NULL;

        if (this->npnEndpoint == NULL) {
          Error("failed to find registered SSL endpoint for '%.*s'", (int)len, (const char *)proto);
          return EVENT_ERROR;
        }

        Debug("ssl", "client selected next protocol '%.*s'", len, proto);
        TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "client selected next protocol'%.*s'", len, proto);
      } else {
        Debug("ssl", "client did not select a next protocol");
        TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "client did not select a next protocol");
      }
    }

    return EVENT_DONE;

  case SSL_ERROR_WANT_CONNECT:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_CONNECT");
    return SSL_HANDSHAKE_WANT_CONNECT;

  case SSL_ERROR_WANT_WRITE:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_WRITE");
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_READ");
    if (retval == -EAGAIN) {
      // No data at the moment, hang tight
      SSLDebugVC(vc, "SSL handshake: EAGAIN");
      return SSL_HANDSHAKE_WANT_READ;
    } else if (retval < 0) {
      // An error, make us go away
      SSLDebugVC(vc, "SSL handshake error: read_retval=%d", retval);
      return EVENT_ERROR;
    }
    return SSL_HANDSHAKE_WANT_READ;

// This value is only defined in openssl has been patched to
// enable the sni callback to break out of the SSL_accept processing
#ifdef SSL_ERROR_WANT_SNI_RESOLVE
  case SSL_ERROR_WANT_X509_LOOKUP:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_X509_LOOKUP");
    return EVENT_CONT;
  case SSL_ERROR_WANT_SNI_RESOLVE:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_SNI_RESOLVE");
#elif SSL_ERROR_WANT_X509_LOOKUP
  case SSL_ERROR_WANT_X509_LOOKUP:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_X509_LOOKUP");
#endif
#if defined(SSL_ERROR_WANT_SNI_RESOLVE) || defined(SSL_ERROR_WANT_X509_LOOKUP)
    if (vc->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL || SSL_HOOK_OP_TUNNEL == hookOpRequested) {
      vc->attributes       = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      sslHandShakeComplete = 0;
      return EVENT_CONT;
    } else {
      //  Stopping for some other reason, perhaps loading certificate
      return SSL_WAIT_FOR_HOOK;
    }
#endif

  case SSL_ERROR_WANT_ACCEPT:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_WANT_ACCEPT");
    return EVENT_CONT;
  }

  if (getTransparentPassThrough()) {
    err = errno;
    SSLDebugVC(vc, "SSL handshake error: %s (%d), errno=%d", SSLErrorName(ssl_error), ssl_error, err);

    // start a blind tunnel if tr-pass is set and data does not look like ClientHello
    SSLDebugVC(vc, "Data does not look like SSL handshake, starting blind tunnel");
    vc->attributes       = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    sslHandShakeComplete = 0;
    return EVENT_CONT;
  }

  switch (ssl_error) {
  case SSL_ERROR_SSL: {
    SSL_CLR_ERR_INCR_DYN_STAT(vc, ssl_error_ssl, "SSLProfileSM::sslServerHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    char buf[512];
    error_code = ERR_peek_last_error();
    ERR_error_string_n(error_code, buf, sizeof(buf));
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(),
            "SSL server handshake ERROR_SSL: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", ssl_error, error_code, buf, errno);
    return EVENT_ERROR;
  }

  case SSL_ERROR_ZERO_RETURN:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_ZERO_RETURN");
    return EVENT_ERROR;
  case SSL_ERROR_SYSCALL:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_SYSCALL");
    return EVENT_ERROR;
  default:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL server handshake ERROR_OTHER");
    return EVENT_ERROR;
  }
}

int
SSLProfileSM::sslClientHandShakeEvent(int &err)
{
  bool trace = this->getTrace();
  ssl_error_t ssl_error;
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);

  ink_assert(SSLProfileSMAccess(ssl) == this);

  ssl_error = SSLConnect(ssl);
  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (is_debug_tag_set("ssl")) {
      X509 *cert = SSL_get_peer_certificate(ssl);

      Debug("ssl", "SSL client handshake completed successfully");
      // if the handshake is complete and write is enabled reschedule the write
      if (netvc->closed == 0 && netvc->write.enabled)
        netvc->writeReschedule();
      if (cert) {
        debug_certificate_name("server certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("server certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }
    SSL_INCREMENT_DYN_STAT(ssl_total_success_handshake_count_out_stat);

    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake completed successfully");
    // do we want to include cert info in trace?

    sslHandShakeComplete = true;
    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_WANT_WRITE");
    SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake ERROR_WANT_WRITE");
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_WANT_READ");
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake ERROR_WANT_READ");
    return SSL_HANDSHAKE_WANT_READ;

  case SSL_ERROR_WANT_X509_LOOKUP:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_WANT_X509_LOOKUP");
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake ERROR_WANT_X509_LOOKUP");
    break;

  case SSL_ERROR_WANT_ACCEPT:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake ERROR_WANT_ACCEPT");
    return SSL_HANDSHAKE_WANT_ACCEPT;

  case SSL_ERROR_WANT_CONNECT:
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake ERROR_WANT_CONNECT");
    break;

  case SSL_ERROR_ZERO_RETURN:
    SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, EOS");
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake EOS");
    return EVENT_ERROR;

  case SSL_ERROR_SYSCALL:
    err = errno;
    SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, syscall");
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(), "SSL client handshake Syscall Error: %s", strerror(errno));
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SSL:
  default: {
    err = errno;
    // FIXME -- This triggers a retry on cases of cert validation errors....
    Debug("ssl", "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_SSL");
    SSL_CLR_ERR_INCR_DYN_STAT(vc, ssl_error_ssl, "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_SSL errno=%d", errno);
    Debug("ssl.error", "SSLProfileSM::sslClientHandShakeEvent, SSL_ERROR_SSL");
    char buf[512];
    error_code = ERR_peek_last_error();
    ERR_error_string_n(error_code, buf, sizeof(buf));
    TraceIn(trace, vc->get_remote_addr(), vc->get_remote_port(),
            "SSL client handshake ERROR_SSL: sslErr=%d, ERR_get_error=%ld (%s) errno=%d", ssl_error, error_code, buf, errno);
    return EVENT_ERROR;
  } break;
  }
  return EVENT_CONT;
}

void
SSLProfileSM::reenable()
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
    if (curHook != NULL) {
      curHook = curHook->next();
    }
    if (curHook != NULL) {
      // Invoke the hook and return, wait for next reenable
      curHook->invoke(TS_EVENT_SSL_CERT, this);
      return;
    } else { // curHook == NULL
      // empty, set state to HOOKS_DONE
      this->sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
    }
  } else if (this->sslHandshakeDoneHookState == SSL_HOOKS_ACTIVE) {
    Debug("ssl", "SSLProfileSM::reenable sslHandshakeDoneHookState = %d, set to SSL_HOOKS_INVOKE", sslHandshakeDoneHookState);
    this->sslHandshakeDoneHookState = SSL_HOOKS_INVOKE;
  }
  vc->readReschedule();
  vc->writeReschedule();
}

bool
SSLProfileSM::callHooks(TSEvent eventId)
{
  // Only dealing with the SNI/CERT hook so far.
  ink_assert(eventId == TS_EVENT_SSL_CERT);
  Debug("ssl", "callHooks sslHandshakeHookState=%d", this->sslHandshakeHookState);

  // First time through, set the type of the hook that is currently being invoked
  if (HANDSHAKE_HOOKS_PRE == sslHandshakeHookState) {
    // the previous hook should be DONE and set curHook to NULL before trigger the sni hook.
    ink_assert(curHook == NULL);
    // set to HOOKS_CERT means CERT/SNI hooks has called by SSL_accept()
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_CERT;
    // get Hooks
    curHook = ssl_hooks->get(TS_SSL_CERT_INTERNAL_HOOK);
  } else {
    // Not in the right state
    // reenable and continue
    return true;
  }

  bool reenabled = true;
  if (curHook != NULL) {
    // Otherwise, we have plugin hooks to run
    this->sslHandshakeHookState = HANDSHAKE_HOOKS_INVOKE;
    curHook->invoke(eventId, this);
    reenabled = (this->sslHandshakeHookState != HANDSHAKE_HOOKS_INVOKE);
  } else {
    // no SNI-Hooks set, set state to HOOKS_DONE
    // no plugins registered for this hook, return (reenabled == true)
    sslHandshakeHookState = HANDSHAKE_HOOKS_DONE;
  }
  return reenabled;
}

bool
SSLProfileSM::computeSSLTrace()
{
// this has to happen before the handshake or else sni_servername will be NULL
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
  const sockaddr *remote_addr = vc->get_remote_addr();
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

const char *
SSLProfileSM::get_protocol_tag() const
{
  const char *retval    = NULL;
  const char *ssl_proto = getSSLProtocol();
  if (ssl_proto && strncmp(ssl_proto, "TLSv1", 5) == 0) {
    if (ssl_proto[5] == '\0') {
      retval = TS_PROTO_TAG_TLS_1_0;
    } else if (ssl_proto[5] == '.') {
      if (ssl_proto[6] == '1' && ssl_proto[7] == '\0') {
        retval = TS_PROTO_TAG_TLS_1_1;
      } else if (ssl_proto[6] == '2' && ssl_proto[7] == '\0') {
        retval = TS_PROTO_TAG_TLS_1_2;
      } else if (ssl_proto[6] == '3' && ssl_proto[7] == '\0') {
        retval = TS_PROTO_TAG_TLS_1_3;
      }
    }
  }
  return retval;
}
