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
#include "ink_config.h"
#include "P_Net.h"
#include "P_SSLNextProtocolSet.h"
#include "P_SSLUtils.h"

#define SSL_READ_ERROR_NONE	  0
#define SSL_READ_ERROR		  1
#define SSL_READ_READY		  2
#define SSL_READ_COMPLETE	  3
#define SSL_READ_WOULD_BLOCK      4
#define SSL_READ_EOS		  5
#define SSL_HANDSHAKE_WANT_READ   6
#define SSL_HANDSHAKE_WANT_WRITE  7
#define SSL_HANDSHAKE_WANT_ACCEPT 8
#define SSL_HANDSHAKE_WANT_CONNECT 9
#define SSL_WRITE_WOULD_BLOCK     10

ClassAllocator<SSLNetVConnection> sslNetVCAllocator("sslNetVCAllocator");

//
// Private
//

static SSL *
make_ssl_connection(SSL_CTX * ctx, SSLNetVConnection * netvc)
{
  SSL * ssl;

  if (likely(ssl = SSL_new(ctx))) {
    SSL_set_fd(ssl, netvc->get_socket());
    SSL_set_app_data(ssl, netvc);
  }

  return ssl;
}

static void
debug_certificate_name(const char * msg, X509_NAME * name)
{
  BIO * bio;

  if (name == NULL) {
    return;
  }

  bio = BIO_new(BIO_s_mem());
  if (bio == NULL) {
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    long len;
    char * ptr;
    len = BIO_get_mem_data(bio, &ptr);
    Debug("ssl", "%s %.*s", msg, (int)len, ptr);
  }

  BIO_free(bio);
}

static inline int
do_SSL_write(SSL * ssl, void *buf, int size)
{
  int r = 0;
  do {
    // need to check into SSL error handling
    // to see if this is good enough.
    r = SSL_write(ssl, (const char *) buf, size);
    if (r >= 0)
      return r;
    else
      r = -errno;
  } while (r == -EINTR || r == -ENOBUFS || r == -ENOMEM);

  return r;
}


static int
ssl_read_from_net(SSLNetVConnection * sslvc, EThread * lthread, int64_t &ret)
{
  NetState *s = &sslvc->read;
  MIOBufferAccessor & buf = s->vio.buffer;
  IOBufferBlock *b = buf.writer()->first_write_block();
  int event = SSL_READ_ERROR_NONE;
  int64_t bytes_read;
  int64_t block_write_avail;
  int sslErr = SSL_ERROR_NONE;

  for (bytes_read = 0; (b != 0) && (sslErr == SSL_ERROR_NONE); b = b->next) {
    block_write_avail = b->write_avail();

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] b->write_avail()=%" PRId64, block_write_avail);

    int64_t offset = 0;
    // while can be replaced with if - need to test what works faster with openssl
    while (block_write_avail > 0) {
      int rres = SSL_read(sslvc->ssl, b->end() + offset, (int)block_write_avail);

      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] rres=%d", rres);

      sslErr = SSL_get_error(sslvc->ssl, rres);
      switch (sslErr) {
      case SSL_ERROR_NONE:

#if DEBUG
        SSLDebugBufferPrint("ssl_buff", b->end() + offset, rres, "SSL Read");
#endif

        ink_assert(rres);

        bytes_read += rres;
        offset += rres;
        block_write_avail -= rres;
        ink_assert(block_write_avail >= 0);

        continue;

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
        event = SSL_READ_WOULD_BLOCK;
        SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);
        Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK(read/x509 lookup)");
        break;
      case SSL_ERROR_SYSCALL:
        SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
        if (rres != 0) {
          // not EOF
          event = SSL_READ_ERROR;
          ret = errno;
          Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
        } else {
          // then EOF observed, treat it as EOS
          event = SSL_READ_EOS;
          //Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, EOF observed violating SSL protocol");
        }
        break;
      case SSL_ERROR_ZERO_RETURN:
        event = SSL_READ_EOS;
        SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
        Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_ZERO_RETURN");
        break;
      case SSL_ERROR_SSL:
      default:
        event = SSL_READ_ERROR;
        ret = errno;
        SSL_INCREMENT_DYN_STAT(ssl_error_ssl);
        Debug("ssl.error", "[SSL_NetVConnection::ssl_read_from_net]");
        break;
      }                         // switch
      break;
    }                           // while( block_write_avail > 0 )
  }                             // for ( bytes_read = 0; (b != 0); b = b->next)

  if (bytes_read > 0) {
    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read=%" PRId64, bytes_read);
    buf.writer()->fill(bytes_read);
    s->vio.ndone += bytes_read;
    sslvc->netActivity(lthread);

    ret = bytes_read;

    if (s->vio.ntodo() <= 0) {
      event = SSL_READ_COMPLETE;
    } else {
      event = SSL_READ_READY;
    }
  } else                        // if( bytes_read > 0 )
  {
#if defined (_DEBUG)
    if (bytes_read == 0) {
      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read == 0");
    }
#endif
  }
  return (event);

}


//changed by YTS Team, yamsat
void
SSLNetVConnection::net_read_io(NetHandler *nh, EThread *lthread)
{
  int ret;
  int64_t r = 0;
  int64_t bytes = 0;
  NetState *s = &this->read;
  MIOBufferAccessor &buf = s->vio.buffer;
  int64_t ntodo = s->vio.ntodo();

  if (sslClientRenegotiationAbort == true) {
    this->read.triggered = 0;
    readSignalError(nh, (int)r);
    Debug("ssl", "[SSLNetVConnection::net_read_io] client renegotiation setting read signal error");
    return;
  }

  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, lthread, s->vio._cont);
  if (!lock) {
    readReschedule(nh);
    return;
  }
  // If it is not enabled, lower its priority.  This allows
  // a fast connection to speed match a slower connection by
  // shifting down in priority even if it could read.
  if (!s->enabled || s->vio.op != VIO::READ) {
    read_disable(nh, this);
    return;
  }

  ink_assert(buf.writer());

  // This function will always return true unless
  // vc is an SSLNetVConnection.
  if (!getSSLHandShakeComplete()) {
    int err;

    if (getSSLClientConnection()) {
      ret = sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = sslStartHandShake(SSL_EVENT_SERVER, err);
    }

    if (ret == EVENT_ERROR) {
      this->read.triggered = 0;
      readSignalError(nh, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT) {
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
        if (read.enabled)
          nh->read_ready_list.in_or_enqueue(this);
      }
    } else
      readReschedule(nh);
    return;
  }

  // If there is nothing to do or no space available, disable connection
  if (ntodo <= 0 || !buf.writer()->write_avail()) {
    read_disable(nh, this);
    return;
  }

  // not sure if this do-while loop is really needed here, please replace this comment if you know
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
  case SSL_READ_ERROR_NONE:
  case SSL_READ_READY:
    readReschedule(nh);
    return;
    break;
  case SSL_WRITE_WOULD_BLOCK:
  case SSL_READ_WOULD_BLOCK:
    if (lock.m.m_ptr != s->vio.mutex.m_ptr) {
      Debug("ssl", "ssl_read_from_net, mutex switched");
      if (ret == SSL_READ_WOULD_BLOCK)
        readReschedule(nh);
      else
        writeReschedule(nh);
      return;
    }
    // reset the trigger and remove from the ready queue
    // we will need to be retriggered to read from this socket again
    read.triggered = 0;
    nh->read_ready_list.remove(this);
    Debug("ssl", "read_from_net, read finished - would block");
#ifdef TS_USE_PORT
    if (ret == SSL_READ_WOULD_BLOCK)
      readReschedule(nh);
    else
      writeReschedule(nh);
#endif
    break;

  case SSL_READ_EOS:
    // close the connection if we have SSL_READ_EOS, this is the return value from ssl_read_from_net() if we get an SSL_ERROR_ZERO_RETURN from SSL_get_error()
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
SSLNetVConnection::load_buffer_and_write(int64_t towrite, int64_t &wattempted, int64_t &total_wrote, MIOBufferAccessor & buf, int &needs)
{
  ProxyMutex *mutex = this_ethread()->mutex;
  int64_t r = 0;
  int64_t l = 0;

  // XXX Rather than dealing with the block directly, we should use the IOBufferReader API.
  int64_t offset = buf.reader()->start_offset;
  IOBufferBlock *b = buf.reader()->block;

  do {
    // check if we have done this block
    l = b->read_avail();
    l -= offset;
    if (l <= 0) {
      offset = -l;
      b = b->next;
      continue;
    }
    // check if to amount to write exceeds that in this buffer
    int64_t wavail = towrite - total_wrote;

    if (l > wavail) {
      l = wavail;
    }

    // TS-2365: If the SSL max record size is set and we have
    // more data than that, break this into smaller write
    // operations.
    int64_t orig_l = l;
    if (SSLConfigParams::ssl_maxrecord > 0 && l > SSLConfigParams::ssl_maxrecord) {
        l = SSLConfigParams::ssl_maxrecord;
    }

    if (!l) {
      break;
    }

    wattempted = l;
    total_wrote += l;
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, before do_SSL_write, l=%" PRId64", towrite=%" PRId64", b=%p",
          l, towrite, b);
    r = do_SSL_write(ssl, b->start() + offset, (int)l);
    if (r == l) {
      wattempted = total_wrote;
    }
    if (l == orig_l) {
        // on to the next block
        offset = 0;
        b = b->next;
    } else {
        offset += l;
    }

    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite,Number of bytes written=%" PRId64" , total=%" PRId64"", r, total_wrote);
    NET_DEBUG_COUNT_DYN_STAT(net_calls_to_write_stat, 1);
  } while (r == l && total_wrote < towrite && b);

  if (r > 0) {
    if (total_wrote != wattempted) {
      Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, wrote some bytes, but not all requested.");
      // I'm not sure how this could happen. We should have tried and hit an EAGAIN.
      needs |= EVENTIO_WRITE;
      return (r);
    } else {
      Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, write successful.");
      return (total_wrote);
    }
  } else {
    int err = SSL_get_error(ssl, (int)r);

    switch (err) {
    case SSL_ERROR_NONE:
      Debug("ssl", "SSL_write-SSL_ERROR_NONE");
      break;
    case SSL_ERROR_WANT_READ:
      needs |= EVENTIO_READ;
      r = -EAGAIN;
      SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_READ");
      break;
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_X509_LOOKUP: {
      if (SSL_ERROR_WANT_WRITE == err)
        SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
      else if (SSL_ERROR_WANT_X509_LOOKUP == err)
        SSL_INCREMENT_DYN_STAT(ssl_error_want_x509_lookup);

      needs |= EVENTIO_WRITE;
      r = -EAGAIN;
      Debug("ssl.error", "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    }
    case SSL_ERROR_SYSCALL:
      r = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_syscall);
      Debug("ssl.error", "SSL_write-SSL_ERROR_SYSCALL");
      break;
      // end of stream
    case SSL_ERROR_ZERO_RETURN:
      r = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_zero_return);
      Debug("ssl.error", "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default:
      r = -errno;
      SSL_INCREMENT_DYN_STAT(ssl_error_ssl);
      Debug("ssl.error", "SSL_write-SSL_ERROR_SSL");
      break;
    }
    return (r);
  }
}

SSLNetVConnection::SSLNetVConnection():
  sslHandShakeComplete(false),
  sslClientConnection(false),
  sslClientRenegotiationAbort(false),
  npnSet(NULL),
  npnEndpoint(NULL)
{
  ssl = NULL;
  sslHandshakeBeginTime = 0;
}

void
SSLNetVConnection::free(EThread * t) {
  NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, -1);
  got_remote_addr = 0;
  got_local_addr = 0;
  read.vio.mutex.clear();
  write.vio.mutex.clear();
  this->mutex.clear();
  flags = 0;
  SET_CONTINUATION_HANDLER(this, (SSLNetVConnHandler) & SSLNetVConnection::startEvent);
  nh = NULL;
  read.triggered = 0;
  write.triggered = 0;
  options.reset();
  closed = 0;
  ink_assert(con.fd == NO_FD);
  if (ssl != NULL) {
    /*if (sslHandShakeComplete)
       SSL_set_shutdown(ssl, SSL_SENT_SHUTDOWN|SSL_RECEIVED_SHUTDOWN); */
    SSL_free(ssl);
    ssl = NULL;
  }
  sslHandShakeComplete = false;
  sslClientConnection = false;
  sslClientRenegotiationAbort = false;
  npnSet = NULL;
  npnEndpoint= NULL;

  if (from_accept_thread) {
    sslNetVCAllocator.free(this);  
  } else {
    THREAD_FREE(this, sslNetVCAllocator, t);
  }
}

int
SSLNetVConnection::sslStartHandShake(int event, int &err)
{

  switch (event) {
  case SSL_EVENT_SERVER:
    if (this->ssl == NULL) {
      SSLCertificateConfig::scoped_config lookup;

      // Attach the default SSL_CTX to this SSL session. The default context is never going to be able
      // to negotiate a SSL session, but it's enough to trampoline us into the SNI callback where we
      // can select the right server certificate.
      this->ssl = make_ssl_connection(lookup->defaultContext(), this);
    }

    if (this->ssl == NULL) {
      SSLErrorVC(this, "failed to create SSL server session");
      return EVENT_ERROR;
    }

    return sslServerHandShakeEvent(err);

  case SSL_EVENT_CLIENT:
    if (this->ssl == NULL) {
      this->ssl = make_ssl_connection(ssl_NetProcessor.client_ctx, this);
    }

    if (this->ssl == NULL) {
      SSLErrorVC(this, "failed to create SSL client session");
      return EVENT_ERROR;
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
  int ret = SSL_accept(ssl);
  int ssl_error = SSL_get_error(ssl, ret);

  if (ssl_error != SSL_ERROR_NONE) {
    err = errno;
    SSLDebugVC(this,"SSL handshake error: %s (%d), errno=%d", SSLErrorName(ssl_error), ssl_error, err);
  }

  switch (ssl_error) {
  case SSL_ERROR_NONE:
    if (is_debug_tag_set("ssl")) {
      X509 * cert = SSL_get_peer_certificate(ssl);

      Debug("ssl", "SSL server handshake completed successfully");
      if (cert) {
        debug_certificate_name("client certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("client certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }

    sslHandShakeComplete = true;

    if (sslHandshakeBeginTime) {
      const ink_hrtime ssl_handshake_time = ink_get_hrtime() - sslHandshakeBeginTime;
      Debug("ssl", "ssl handshake time:%" PRId64, ssl_handshake_time);
      sslHandshakeBeginTime = 0;
      SSL_INCREMENT_DYN_STAT_EX(ssl_total_handshake_time_stat, ssl_handshake_time);
      SSL_INCREMENT_DYN_STAT(ssl_total_success_handshake_count_stat);
    }

    {
      const unsigned char * proto = NULL;
      unsigned len = 0;

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
        this->npnSet = NULL;

        if (this->npnEndpoint == NULL) {
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

  case SSL_ERROR_WANT_ACCEPT:
  case SSL_ERROR_WANT_X509_LOOKUP:
    return EVENT_CONT;

  case SSL_ERROR_ZERO_RETURN:
  case SSL_ERROR_SYSCALL:
  case SSL_ERROR_SSL:
  default:
    return EVENT_ERROR;
  }

}


int
SSLNetVConnection::sslClientHandShakeEvent(int &err)
{
  int ret;

#if TS_USE_TLS_SNI
  if (options.sni_servername) {
    if (SSL_set_tlsext_host_name(ssl, options.sni_servername)) {
      Debug("ssl", "using SNI name '%s' for client handshake", options.sni_servername);
    } else {
      Debug("ssl.error","failed to set SNI name '%s' for client handshake", options.sni_servername);
      SSL_INCREMENT_DYN_STAT(ssl_sni_name_set_failure);
    }
  }
#endif

  ret = SSL_connect(ssl);
  switch (SSL_get_error(ssl, ret)) {
  case SSL_ERROR_NONE:
    if (is_debug_tag_set("ssl")) {
      X509 * cert = SSL_get_peer_certificate(ssl);

      Debug("ssl", "SSL client handshake completed successfully");
      // if the handshake is complete and write is enabled reschedule the write
      if (closed == 0 && write.enabled)
        writeReschedule(nh);
      if (cert) {
        debug_certificate_name("server certificate subject CN is", X509_get_subject_name(cert));
        debug_certificate_name("server certificate issuer CN is", X509_get_issuer_name(cert));
        X509_free(cert);
      }
    }

    sslHandShakeComplete = true;
    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_WRITE");
    SSL_INCREMENT_DYN_STAT(ssl_error_want_write);
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    SSL_INCREMENT_DYN_STAT(ssl_error_want_read);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_WANT_READ");
    return SSL_HANDSHAKE_WANT_READ;

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
  default:
    err = errno;
    SSL_INCREMENT_DYN_STAT(ssl_error_ssl);
    Debug("ssl.error", "SSLNetVConnection::sslClientHandShakeEvent, SSL_ERROR_SSL");
    return EVENT_ERROR;
    break;

  }
  return EVENT_CONT;

}

void
SSLNetVConnection::registerNextProtocolSet(const SSLNextProtocolSet * s)
{
  ink_release_assert(this->npnSet == NULL);
  this->npnSet = s;
}

// NextProtocolNegotiation TLS extension callback. The NPN extension
// allows the client to select a preferred protocol, so all we have
// to do here is tell them what out protocol set is.
int
SSLNetVConnection::advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned int *outlen,
                                           void * /*arg ATS_UNUSED */)
{
  SSLNetVConnection * netvc = (SSLNetVConnection *)SSL_get_app_data(ssl);

  ink_release_assert(netvc != NULL);

  if (netvc->npnSet && netvc->npnSet->advertiseProtocols(out, outlen)) {
    // Successful return tells OpenSSL to advertise.
    return SSL_TLSEXT_ERR_OK;
  }

  return SSL_TLSEXT_ERR_NOACK;
}

// ALPN TLS extension callback. Given the client's set of offered
// protocols, we have to select a protocol to use for this session.
int
SSLNetVConnection::select_next_protocol(SSL * ssl, const unsigned char ** out, unsigned char * outlen, const unsigned char * in ATS_UNUSED, unsigned inlen ATS_UNUSED, void *)
{
  SSLNetVConnection * netvc = (SSLNetVConnection *)SSL_get_app_data(ssl);
  const unsigned char * npn = NULL;
  unsigned npnsz = 0;

  ink_release_assert(netvc != NULL);

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

  *out = NULL;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}
