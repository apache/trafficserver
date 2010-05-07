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

#ifdef HAVE_LIBSSL
#include "P_Net.h"

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
ClassAllocator<SSLNetVConnection> sslNetVCAllocator("sslNetVCAllocator");


//
// Private
//

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
ssl_read_from_net(NetHandler * nh, UnixNetVConnection * vc, EThread * lthread, int &ret)
{
  NetState *s = &vc->read;
  SSLNetVConnection *sslvc = (SSLNetVConnection *) vc;
  MIOBufferAccessor & buf = s->vio.buffer;
  IOBufferBlock *b = buf.mbuf->_writer;
  int event = SSL_READ_ERROR_NONE;
  int bytes_read;
  int block_write_avail;
  int sslErr = SSL_ERROR_NONE;

  for (bytes_read = 0; (b != 0) && (sslErr == SSL_ERROR_NONE); b = b->next) {
    block_write_avail = b->write_avail();

    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] b->write_avail()=%d", block_write_avail);

    int offset = 0;
    // while can be replaced with if - need to test what works faster with openssl
    while (block_write_avail > 0) {
      sslvc->read_calls++;
      int rres = SSL_read(sslvc->ssl, b->end() + offset, block_write_avail);

      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] rres=%d", rres);

      sslErr = SSL_get_error(sslvc->ssl, rres);
      switch (sslErr) {
      case SSL_ERROR_NONE:

        DebugBufferPrint("ssl_buff", b->end() + offset, rres, "SSL Read");
        ink_debug_assert(rres);

        bytes_read += rres;
        offset += rres;
        block_write_avail -= rres;
        ink_debug_assert(block_write_avail >= 0);

        continue;

      case SSL_ERROR_WANT_WRITE:
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_X509_LOOKUP:
        event = SSL_READ_WOULD_BLOCK;
        Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_WOULD_BLOCK");
        break;
      case SSL_ERROR_SYSCALL:
        if (rres != 0) {
          // not EOF
          event = SSL_READ_ERROR;
          ret = errno;
          Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, underlying IO error: %s", strerror(errno));
        } else {
          // then EOF observed, treat it as EOS
          event = SSL_READ_EOS;
          //Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SYSCALL, EOF observed violating SSL protocol");
        }
        break;
      case SSL_ERROR_ZERO_RETURN:
        event = SSL_READ_EOS;
        Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_ZERO_RETURN");
        break;
      case SSL_ERROR_SSL:
      default: {
        char err_string[4096];
        ERR_error_string(ERR_get_error(), err_string);
        event = SSL_READ_ERROR;
        ret = errno;
        Error("[SSL_NetVConnection::ssl_read_from_net] SSL_ERROR_SSL %s", err_string);
        break;
      }
      }                         // switch
      break;
    }                           // while( block_write_avail > 0 )
  }                             // for ( bytes_read = 0; (b != 0); b = b->next)

  if (bytes_read > 0) {
    Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] bytes_read=%d", bytes_read);
    buf.writer()->fill(bytes_read);
    s->vio.ndone += bytes_read;
    vc->netActivity(lthread);

    ret = bytes_read;

    if (s->vio.ntodo() <= 0) {
      event = SSL_READ_COMPLETE;
    } else {
      event = SSL_READ_READY;
    }
  } else                        // if( bytes_read > 0 )
  {
#if defined (_DEBUG)
    if (readAvail == 0) {
      Debug("ssl", "[SSL_NetVConnection::ssl_read_from_net] readAvail == 0");
    }
#endif
  }
  return (event);

}


//changed by YTS Team, yamsat
void
SSLNetVConnection::net_read_io(NetHandler * nh, EThread * lthread)
{
  int ret;
  int r = 0;
  int bytes = 0;
  NetState *s = &this->read;
  MIOBufferAccessor & buf = s->vio.buffer;
  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, lthread, s->vio._cont);
  if (!lock) {
    return;
  }
  // If it is not enabled, lower its priority.  This allows 
  // a fast connection to speed match a slower connection by 
  // shifting down in priority even if it could read. 
  if (!s->enabled || s->vio.op != VIO::READ) {
    read_disable(nh, this);
    return;
  }

  ink_debug_assert(buf.writer());

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
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT || ret == SSL_HANDSHAKE_WANT_CONNECT
               || ret == SSL_HANDSHAKE_WANT_WRITE) {
      read.triggered = 0;
      nh->read_ready_list.remove(this);
      write.triggered = 0;
      nh->write_ready_list.remove(this);
    } else if (ret == EVENT_DONE) {
      write.triggered = 1;
      if (write.enabled)
        nh->write_ready_list.in_or_enqueue(this);
    } else 
      readReschedule(nh);
    return;
  }
  // If there is nothing to do, disable connection 
  int ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    read_disable(nh, this);
    return;
  }

  do {
    if (!buf.writer()->write_avail()) {
      buf.writer()->add_block();
    }
    ret = ssl_read_from_net(nh, this, lthread, r);
    if (ret == SSL_READ_READY || ret == SSL_READ_ERROR_NONE) {
      bytes += r;
    }

  } while (ret == SSL_READ_READY || ret == SSL_READ_ERROR_NONE);

  if (bytes > 0) {
    if (ret == SSL_READ_WOULD_BLOCK) {
      if (readSignalAndUpdate(VC_EVENT_READ_READY) != EVENT_CONT) {
        Debug("ssl", "ssl_read_from_net, readSignal !=EVENT_CONT");
        return;
      }
    }
  }

  switch (ret) {
  case SSL_READ_ERROR_NONE:
  case SSL_READ_READY:
    // how did we exit the while loop above? should never happen.
    ink_debug_assert(false);
    break;
  case SSL_READ_WOULD_BLOCK:
    if (lock.m.m_ptr != s->vio.mutex.m_ptr) {
      Debug("ssl", "ssl_read_from_net, mutex switched");
      readReschedule(nh);
      return;
    }
    // reset the tigger and remove from the ready queue
    // we will need to be retriggered to read from this socket again
    read.triggered = 0;
    nh->read_ready_list.remove(this);
    Debug("ssl", "read_from_net, read finished - would block");
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
    readSignalError(nh, r);
    Debug("ssl", "read_from_net, read finished - read error");
    break;
  }

}


ink64 SSLNetVConnection::load_buffer_and_write(ink64 towrite, ink64 &wattempted, ink64 &total_wrote, MIOBufferAccessor & buf) {
  ProxyMutex *mutex = this_ethread()->mutex;
  int r = 0;
  ink64 l = 0;
  int offset = buf.entry->start_offset;
  IOBufferBlock *b = buf.entry->block;

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
    ink64 wavail = towrite - total_wrote;
    if (l > wavail)
      l = wavail;
    if (!l)
      break;
    wattempted = l;
    total_wrote += l;
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, before do_SSL_write, l = %d, towrite = %d, b = %x", l,
          towrite, b);
    write_calls++;
    r = do_SSL_write(ssl, b->start() + offset, l);
    if (r == l) {
      wattempted = total_wrote;
    }
    // on to the next block
    offset = 0;
    b = b->next;
    Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite,Number of bytes written =%d , total =%d", r, total_wrote);
    NET_DEBUG_COUNT_DYN_STAT(net_calls_to_write_stat, 1);
  } while (r == l && total_wrote < towrite && b);
  if (r > 0) {
    if (total_wrote != wattempted) {
      Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, wrote some bytes, but not all requested.");
      return (r);
    } else {
      Debug("ssl", "SSLNetVConnection::loadBufferAndCallWrite, write successful.");
      return (total_wrote);
    }
  } else {
    int err = SSL_get_error(ssl, r);
    switch (err) {
    case SSL_ERROR_NONE:
      Debug("ssl", "SSL_write-SSL_ERROR_NONE");
      break;
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_X509_LOOKUP:
      r = -EAGAIN;
      Debug("ssl", "SSL_write-SSL_ERROR_WANT_WRITE");
      break;
    case SSL_ERROR_SYSCALL:
      r = -errno;
      Debug("ssl", "SSL_write-SSL_ERROR_SYSCALL");
      break;
      // end of stream
    case SSL_ERROR_ZERO_RETURN:
      r = -errno;
      Debug("ssl", "SSL_write-SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_SSL:
    default:
      r = -errno;
      Debug("ssl", "SSL_write-SSL_ERROR_SSL");
      SSLNetProcessor::logSSLError("SSL_write");
      break;
    }
    return (r);
  }
}

SSLNetVConnection::SSLNetVConnection():
  connect_calls(0),
  connect_want_write(0),
  connect_want_read(0),
  connect_want_connect(0),
  connect_want_ssl(0),
  connect_want_syscal(0),
  connect_want_accept(0),
  connect_want_x509(0),
  connect_error_zero(0),
  accept_calls(0),
  read_calls(0),
  read_want_write(0),
  read_want_read(0),
  read_want_ssl(0),
  read_want_syscal(0),
  read_want_x509(0),
  read_error_zero(0),
  write_calls(0),
  write_want_write(0),
  write_want_read(0),
  write_want_ssl(0),
  write_want_syscal(0), write_want_x509(0), write_error_zero(0), sslHandShakeComplete(false), sslClientConnection(false)
{
  ssl = NULL;
}

void
SSLNetVConnection::free(EThread * t) {
  ProxyMutex *mutex = t->mutex;
  NET_DECREMENT_DYN_STAT(net_connections_currently_open_stat);
  got_remote_addr = 0;
  got_local_addr = 0;
  read.vio.mutex.clear();
  write.vio.mutex.clear();
  action_.mutex.clear();
  this->mutex.clear();
  flags = 0;
  SET_CONTINUATION_HANDLER(this, (SSLNetVConnHandler) & SSLNetVConnection::startEvent);
  ink_assert(con.fd == NO_FD);
  nh = NULL;
  read_calls = 0;
  write_calls = 0;
  connect_calls = 0;
  accept_calls = 0;
  connect_want_write = 0;
  connect_want_read = 0;
  connect_want_connect = 0;
  connect_want_ssl = 0;
  connect_want_syscal = 0;
  connect_want_accept = 0;
  connect_want_x509 = 0;
  connect_error_zero = 0;
  read_want_write = 0;
  read_want_read = 0;
  read_want_ssl = 0;
  read_want_syscal = 0;
  read_want_x509 = 0;
  read_error_zero = 0;
  write_want_write = 0;
  write_want_read = 0;
  write_want_ssl = 0;
  write_want_syscal = 0;
  write_want_x509 = 0;
  write_error_zero = 0;
  if (ssl != NULL) {
    /*if (sslHandShakeComplete)
       SSL_set_shutdown(ssl, SSL_SENT_SHUTDOWN|SSL_RECEIVED_SHUTDOWN); */
    SSL_free(ssl);
    ssl = NULL;
  }
  sslHandShakeComplete = 0;
  sslClientConnection = 0;
  THREAD_FREE(this, sslNetVCAllocator, t);
}

int
SSLNetVConnection::sslStartHandShake(int event, int &err)
{
  SSL_CTX *ctx = NULL;
  struct sockaddr_in sa;
  struct in_addr ia;
  int namelen = sizeof(sa);
  char *strAddr;

  if (event == SSL_EVENT_SERVER) {
    if (ssl == NULL) {
      if (sslCertLookup.multipleCerts) {
        safe_getsockname(get_socket(), (struct sockaddr *) &sa, &namelen);
        ia.s_addr = sa.sin_addr.s_addr;
        strAddr = inet_ntoa(ia);
        ctx = sslCertLookup.findInfoInHash(strAddr);
        if (ctx == NULL)
          ctx = ssl_NetProcessor.ctx;
      } else {
        ctx = ssl_NetProcessor.ctx;
      }
      ssl = SSL_new(ctx);
      if (ssl != NULL) {
        SSL_set_fd(ssl, get_socket());
      } else {
        Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, ssl create failed");
      }

    }
    return (sslServerHandShakeEvent(err));
  } else {
    if (ssl == NULL) {
      ssl = SSL_new(ssl_NetProcessor.client_ctx);
      SSL_set_fd(ssl, get_socket());
    }
    ink_assert(event == SSL_EVENT_CLIENT);
    return (sslClientHandShakeEvent(err));
  }

}

int
SSLNetVConnection::sslServerHandShakeEvent(int &err)
{

  int ret;
  accept_calls++;
  //printf("calling SSL_accept for fd %d\n",con.fd);
  ret = SSL_accept(ssl);

  switch (SSL_get_error(ssl, ret)) {
  case SSL_ERROR_NONE:
    Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, handshake completed successfully");
    client_cert = SSL_get_peer_certificate(ssl);
    if (client_cert != NULL) {
/*		str = X509_NAME_oneline (X509_get_subject_name (client_cert), 0, 0);
		Free (str);
    
		str = X509_NAME_oneline (X509_get_issuer_name  (client_cert), 0, 0);
		Free (str);
    
		// Add any extra client cert verification stuff here.  SSL
		// is set up in SSLNetProcessor::start to automatically verify
		// the client cert's CA, if required.
*/
      X509_free(client_cert);
    }
    sslHandShakeComplete = 1;

    return EVENT_DONE;

  case SSL_ERROR_WANT_ACCEPT:
    break;

  case SSL_ERROR_WANT_CONNECT:
    return SSL_HANDSHAKE_WANT_CONNECT;

  case SSL_ERROR_WANT_WRITE:
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    return SSL_HANDSHAKE_WANT_READ;
  case SSL_ERROR_WANT_X509_LOOKUP:
    Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, would block on read or write");
    break;

  case SSL_ERROR_ZERO_RETURN:
    Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, EOS");
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SYSCALL:
    err = errno;
    Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, syscall");
    return EVENT_ERROR;
    break;

  case SSL_ERROR_SSL:
  default:
    err = errno;
    Debug("ssl", "SSLNetVConnection::sslServerHandShakeEvent, error");
    SSLNetProcessor::logSSLError("SSL_ServerHandShake");
    return EVENT_ERROR;
    break;
  }
  return EVENT_CONT;
}


int
SSLNetVConnection::sslClientHandShakeEvent(int &err)
{
  int ret;
  connect_calls++;
  //printf("calling connect for fd %d\n",con.fd);
  ret = SSL_connect(ssl);
  switch (SSL_get_error(ssl, ret)) {
  case SSL_ERROR_NONE:
    Debug("ssl", "SSLNetVConnection::sslClientHandShakeEvent, handshake completed successfully");
    server_cert = SSL_get_peer_certificate(ssl);

/*	  str = X509_NAME_oneline (X509_get_subject_name (server_cert),0,0);
	  Free (str);

	  str = X509_NAME_oneline (X509_get_issuer_name  (server_cert),0,0);
	  Free (str);
*/

/*	 Add certificate verification stuff here before
     deallocating the certificate. 
*/

    X509_free(server_cert);
    sslHandShakeComplete = 1;

    return EVENT_DONE;

  case SSL_ERROR_WANT_WRITE:
    connect_want_write++;
    return SSL_HANDSHAKE_WANT_WRITE;

  case SSL_ERROR_WANT_READ:
    connect_want_read++;
    return SSL_HANDSHAKE_WANT_READ;

  case SSL_ERROR_WANT_X509_LOOKUP:
    connect_want_x509++;
    Debug("ssl", "SSLNetVConnection::sslClientHandShakeEvent, would block on read or write");
    break;

  case SSL_ERROR_WANT_ACCEPT:
    connect_want_accept++;
    return SSL_HANDSHAKE_WANT_ACCEPT;

  case SSL_ERROR_WANT_CONNECT:
    connect_want_connect++;
    break;

  case SSL_ERROR_ZERO_RETURN:
    connect_error_zero++;
    Debug("ssl", "SSLNetVConnection::sslClientHandShakeEvent, EOS");
    return EVENT_ERROR;

  case SSL_ERROR_SYSCALL:
    connect_want_syscal++;
    err = errno;
    Debug("ssl", "SSLNetVConnection::sslClientHandShakeEvent, syscall");
    return EVENT_ERROR;
    break;


  case SSL_ERROR_SSL:
  default:
    connect_want_ssl++;
    err = errno;
    SSLNetProcessor::logSSLError("sslClientHandShakeEvent");
    return EVENT_ERROR;
    break;

  }
  return EVENT_CONT;

}

#endif
