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

/****************************************************************************

  SSLNetVConnection.h

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/
#pragma once

#include "ts/apidefs.h"

#include "P_UnixNetVConnection.h"
#include "iocore/net/TLSALPNSupport.h"
#include "iocore/net/TLSSessionResumptionSupport.h"
#include "iocore/net/TLSSNISupport.h"
#include "iocore/net/TLSEarlyDataSupport.h"
#include "iocore/net/TLSTunnelSupport.h"
#include "iocore/net/TLSBasicSupport.h"
#include "iocore/net/TLSEventSupport.h"
#include "iocore/net/TLSCertSwitchSupport.h"
#include "P_SSLUtils.h"
#include "P_SSLConfig.h"

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/objects.h>

#include <cstring>
#include <memory>
#include <string_view>

// These are included here because older OpenSSL libraries don't have them.
// Don't copy these defines, or use their values directly, they are merely
// here to avoid compiler errors.
#ifndef SSL_TLSEXT_ERR_OK
#define SSL_TLSEXT_ERR_OK 0
#endif

#ifndef SSL_TLSEXT_ERR_NOACK
#define SSL_TLSEXT_ERR_NOACK 3
#endif

constexpr char SSL_OP_HANDSHAKE = 0x16;

// TS-2503: dynamic TLS record sizing
// For smaller records, we should also reserve space for various TCP options
// (timestamps, SACKs.. up to 40 bytes [1]), and account for TLS record overhead
// (another 20-60 bytes on average, depending on the negotiated ciphersuite [2]).
// All in all: 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
// For larger records, the size is determined by TLS protocol record size
constexpr uint32_t SSL_DEF_TLS_RECORD_SIZE           = 1300; // 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
constexpr uint32_t SSL_MAX_TLS_RECORD_SIZE           = 16383; // 2^14 - 1
constexpr int64_t  SSL_DEF_TLS_RECORD_BYTE_THRESHOLD = 1000000;
constexpr int      SSL_DEF_TLS_RECORD_MSEC_THRESHOLD = 1000;

struct SSLCertLookup;

enum class SslVConnOp {
  SSL_HOOK_OP_DEFAULT,  ///< Null / initialization value. Do normal processing.
  SSL_HOOK_OP_TUNNEL,   ///< Switch to blind tunnel
  SSL_HOOK_OP_TERMINATE ///< Termination connection / transaction.
};

enum class SSLHandshakeStatus { SSL_HANDSHAKE_ONGOING, SSL_HANDSHAKE_DONE, SSL_HANDSHAKE_ERROR };

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////
class SSLNetVConnection : public UnixNetVConnection,
                          public ALPNSupport,
                          public TLSSessionResumptionSupport,
                          public TLSSNISupport,
                          public TLSEarlyDataSupport,
                          public TLSTunnelSupport,
                          public TLSCertSwitchSupport,
                          public TLSEventSupport,
                          public TLSBasicSupport
{
  using super = UnixNetVConnection; ///< Parent type.

public:
  int  sslStartHandShake(int event, int &err) override;
  void clear() override;
  void free_thread(EThread *t) override;

  bool
  trackFirstHandshake() override
  {
    bool retval = this->get_tls_handshake_begin_time() == 0;
    if (retval) {
      this->_record_tls_handshake_begin_time();
    }
    return retval;
  }

  bool
  getSSLHandShakeComplete() const override
  {
    return sslHandshakeStatus != SSLHandshakeStatus::SSL_HANDSHAKE_ONGOING;
  }

  virtual void
  setSSLHandShakeComplete(SSLHandshakeStatus state)
  {
    sslHandshakeStatus = state;
  }

  int     sslServerHandShakeEvent(int &err);
  int     sslClientHandShakeEvent(int &err);
  void    net_read_io(NetHandler *nh) override;
  int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) override;
  void    do_io_close(int lerrno = -1) override;

  ////////////////////////////////////////////////////////////
  // Instances of NetVConnection should be allocated        //
  // only from the free list using NetVConnection::alloc(). //
  // The constructor is public just to avoid compile errors.//
  ////////////////////////////////////////////////////////////
  SSLNetVConnection();
  ~SSLNetVConnection() override {}

  bool
  getSSLClientRenegotiationAbort() const
  {
    return sslClientRenegotiationAbort;
  }

  void
  setSSLClientRenegotiationAbort(bool state)
  {
    sslClientRenegotiationAbort = state;
  }

  bool
  getTransparentPassThrough() const
  {
    return transparentPassThrough;
  }

  void
  setTransparentPassThrough(bool val)
  {
    transparentPassThrough = val;
  }

  bool
  getAllowPlain() const
  {
    return allowPlain;
  }

  void
  setAllowPlain(bool val)
  {
    allowPlain = val;
  }

  // Copy up here so we overload but don't override
  using super::reenable;

  int64_t read_raw_data();

  /** Initialize handshake buffers in which we store TLS handshake data.
   *
   * Typically, we would configure the SSL object to use the socket directly,
   * and call SSL_read on the socket. In this way, the OpenSSL machine would
   * read and parse the stream for us, handshake and all. We cannot, however,
   * blindly let OpenSSL read off the socket since we may need to replay the
   * CLIENT_HELLO raw bytes to the origin if we wind up blind tunneling the
   * connection.  Therefore, for the initial CLIENT_HELLO, we:
   *
   * 1. Manually read bytes off the socket via read_raw_data().
   * 2. Store the bytes in @a handShakeBuffer.
   * 3. Configure our SSL object to read from a memory buffer populated from @a
   *    handshakeReader.
   *
   * Once the CLIENT_HELLO is parsed, we either configure the SSL object to read
   * from the socket as normal, or we replay the bytes to the origin via @a
   * handshakeHolder if we decide to blind tunnel the connection. In the latter
   * tunnel case, any subsequent bytes are blindly tunneled between the origin
   * and the client.
   */
  void
  initialize_handshake_buffers()
  {
    this->handShakeBuffer             = new_MIOBuffer(SSLConfigParams::ssl_misc_max_iobuffer_size_index);
    this->handShakeReader             = this->handShakeBuffer->alloc_reader();
    this->handShakeHolder             = this->handShakeReader->clone();
    this->handShakeBioStored          = 0;
    this->coalescedHandShakeBioBuffer = nullptr;
  }

  void
  free_handshake_buffers()
  {
    if (this->handShakeReader) {
      this->handShakeReader->dealloc();
    }
    if (this->handShakeHolder) {
      this->handShakeHolder->dealloc();
    }
    if (this->handShakeBuffer) {
      free_MIOBuffer(this->handShakeBuffer);
    }
    if (this->coalescedHandShakeBioBuffer != nullptr) {
      ats_free(this->coalescedHandShakeBioBuffer);
    }
    this->handShakeReader             = nullptr;
    this->handShakeHolder             = nullptr;
    this->handShakeBuffer             = nullptr;
    this->handShakeBioStored          = 0;
    this->coalescedHandShakeBioBuffer = nullptr;
  }

  int         populate_protocol(std::string_view *results, int n) const override;
  const char *protocol_contains(std::string_view tag) const override;

  /**
   * Populate the current object based on the socket information in the
   * con parameter and the ssl object in the arg parameter
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  int populate(Connection &con, Continuation *c, void *arg) override;

  SSL       *ssl               = nullptr;
  ink_hrtime sslLastWriteTime  = 0;
  int64_t    sslTotalBytesSent = 0;

  std::shared_ptr<SSL_SESSION> client_sess = nullptr;

  /// Set by asynchronous hooks to request a specific operation.
  SslVConnOp hookOpRequested = SslVConnOp::SSL_HOOK_OP_DEFAULT;

  // noncopyable
  SSLNetVConnection(const SSLNetVConnection &)            = delete;
  SSLNetVConnection &operator=(const SSLNetVConnection &) = delete;

  bool          protocol_mask_set = false;
  unsigned long protocol_mask     = 0;

  bool
  peer_provided_cert() const override
  {
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(this->ssl);
#else
    X509 *cert = SSL_get_peer_certificate(this->ssl);
#endif
    if (cert != nullptr) {
      X509_free(cert);
      return true;
    } else {
      return false;
    }
  }

  int
  provided_cert() const override
  {
    if (this->get_context() == NET_VCONNECTION_OUT) {
      return this->sent_cert;
    } else {
      return 1;
    }
  }

  void
  set_sent_cert(int send_the_cert)
  {
    sent_cert = send_the_cert;
  }

  void set_ca_cert_file(std::string_view file, std::string_view dir);

  const char *
  get_ca_cert_file()
  {
    return _ca_cert_file.get();
  }
  const char *
  get_ca_cert_dir()
  {
    return _ca_cert_dir.get();
  }

  // TLSEventSupport
  /// Reenable the VC after a pre-accept or SNI hook is called.
  void            reenable(int event = TS_EVENT_CONTINUE) override;
  Continuation   *getContinuationForTLSEvents() override;
  EThread        *getThreadForTLSEvents() override;
  Ptr<ProxyMutex> getMutexForTLSEvents() override;

protected:
  // TLSBasicSupport
  SSL *
  _get_ssl_object() const override
  {
    return this->ssl;
  }
  ssl_curve_id _get_tls_curve() const override;
  int          _verify_certificate(X509_STORE_CTX *ctx) override;

  // TLSSessionResumptionSupport
  const IpEndpoint &
  _getLocalEndpoint() override
  {
    return local_addr;
  }

  // TLSSNISupport
  in_port_t _get_local_port() override;

  bool           _isTryingRenegotiation() const override;
  shared_SSL_CTX _lookupContextByName(const std::string &servername, SSLCertContextType ctxType) override;
  shared_SSL_CTX _lookupContextByIP() override;

  // TLSEventSupport
  bool
  _is_tunneling_requested() const override
  {
    return SslVConnOp::SSL_HOOK_OP_TUNNEL == hookOpRequested;
  }
  void
  _switch_to_tunneling_mode() override
  {
    this->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
  }

private:
  std::string_view map_tls_protocol_to_tag(const char *proto_string) const;
  bool             update_rbio(bool move_to_socket);
  void             increment_ssl_version_metric(int version) const;
  NetProcessor    *_getNetProcessor() override;
  void            *_prepareForMigration() override;

  /** Return the unconsumed bytes in @a handShakeReader in a contiguous memory buffer.
   *
   * If @a handShakeReader is a single IOBufferBlock, this returns the pointer
   * to the data in that block. Otherwise, memory is allocated in @a
   * handshakeReaderCoalesced and the bytes are copied into it. Regardless, any
   * previously allocated memory in @a coalescedHandShakeBioBuffer is freed when
   * this function is called.
   *
   * @param[in] total_chain_size The total size of the bytes in @a
   * handShakeReader across all IOBufferBlocks.
   *
   * @return A pointer to all unconsumed bytes in @a handShakeReader in a single
   * contiguous memory buffer.
   */
  char *_getCoalescedHandShakeBuffer(int64_t total_chain_size);

  enum SSLHandshakeStatus sslHandshakeStatus          = SSLHandshakeStatus::SSL_HANDSHAKE_ONGOING;
  bool                    sslClientRenegotiationAbort = false;
  bool                    first_ssl_connect           = true;

  /** The buffer storing the initial CLIENT_HELLO bytes. */
  MIOBuffer *handShakeBuffer = nullptr;

  /** Used to incrementally shuffle bytes read off the socket to the SSL object. */
  IOBufferReader *handShakeHolder = nullptr;

  /** If blind tunneling, this supplies the initial raw bytes of the CLIENT_HELLO. */
  IOBufferReader *handShakeReader = nullptr;

  /** A buffer for the Coalesced @a handShakeReader bytes if @a handShakeReader
   * spans multiple IOBufferBlocks. */
  char *coalescedHandShakeBioBuffer = nullptr;

  /** The number of bytes last send to the SSL's BIO. */
  int handShakeBioStored = 0;

  /** Whether we have already checked for Proxy Protocol in the initial packet. */
  bool haveCheckedProxyProtocol = false;

  bool transparentPassThrough = false;
  bool allowPlain             = false;

  int sent_cert = 0;

  int64_t redoWriteSize = 0;

  // Null-terminated string, or nullptr if there is no SNI server name.
  std::unique_ptr<char[]> _ca_cert_file;
  std::unique_ptr<char[]> _ca_cert_dir;

  ReadWriteEventIO async_ep{};

  // early data related stuff
#if TS_HAS_TLS_EARLY_DATA
  bool            _early_data_finish = false;
  MIOBuffer      *_early_data_buf    = nullptr;
  IOBufferReader *_early_data_reader = nullptr;
#endif

private:
  void                _make_ssl_connection(SSL_CTX *ctx);
  void                _bindSSLObject();
  void                _unbindSSLObject();
  UnixNetVConnection *_migrateFromSSL();
  void                _propagateHandShakeBuffer(UnixNetVConnection *target, EThread *t);

  int         _ssl_read_from_net(int64_t &ret);
  ssl_error_t _ssl_read_buffer(void *buf, int64_t nbytes, int64_t &nread);
  ssl_error_t _ssl_write_buffer(const void *buf, int64_t nbytes, int64_t &nwritten);
  ssl_error_t _ssl_connect();
  ssl_error_t _ssl_accept();

  void _in_context_tunnel() override;
  void _out_context_tunnel() override;
};

using SSLNetVConnHandler = int (SSLNetVConnection::*)(int, void *);

extern ClassAllocator<SSLNetVConnection> sslNetVCAllocator;
