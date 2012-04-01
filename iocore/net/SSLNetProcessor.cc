/** @file

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
#include "I_Layout.h"
#include "I_RecHttp.h"

#include <openssl/engine.h>

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD
typedef const SSL_METHOD * ink_ssl_method_t;
#else
typedef SSL_METHOD * ink_ssl_method_t;
#endif

//
// Global Data
//

SSLNetProcessor ssl_NetProcessor;
NetProcessor & sslNetProcessor = ssl_NetProcessor;

EventType SSLNetProcessor::ET_SSL;

void sslLockingCallback(int mode, int type, const char *file, int line);
unsigned long SSL_pthreads_thread_id();

bool SSLNetProcessor::open_ssl_initialized = false;

static int
SSL_CTX_add_extra_chain_cert_file(SSL_CTX * ctx, const char *file)
{
  BIO *in;
  int ret = 0;
  X509 *x = NULL;

  in = BIO_new(BIO_s_file_internal());
  if (in == NULL) {
    SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, ERR_R_BUF_LIB);
    goto end;
  }

  if (BIO_read_filename(in, file) <= 0) {
    SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, ERR_R_SYS_LIB);
    goto end;
  }

  // j = ERR_R_PEM_LIB;
  while ((x = PEM_read_bio_X509(in, NULL, ctx->default_passwd_callback, ctx->default_passwd_callback_userdata)) != NULL) {
    ret = SSL_CTX_add_extra_chain_cert(ctx, x);
    if (!ret) {
        X509_free(x);
        BIO_free(in);
	return -1;
     }
    }
/*  x = PEM_read_bio_X509(in, NULL, ctx->default_passwd_callback, ctx->default_passwd_callback_userdata);
  if (x == NULL) {
    SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, j);
    goto end;
  }

  ret = SSL_CTX_add_extra_chain_cert(ctx, x);*/
end:
  //  if (x != NULL) X509_free(x);
  if (in != NULL)
    BIO_free(in);
  return (ret);
}

void
SSLNetProcessor::cleanup(void)
{
  if (sslMutexArray) {
    CRYPTO_set_locking_callback(NULL);
    CRYPTO_set_id_callback(NULL);
    for (int i = 0; i < CRYPTO_num_locks(); i++) {
      sslMutexArray[i]->free();
    }
    OPENSSL_free(sslMutexArray);
    sslMutexArray = NULL;
  }

  if (client_ctx)
    SSL_CTX_free(client_ctx);
  client_ctx = NULL;
}

void
SSLNetProcessor::initSSLLocks(void)
{

  sslMutexArray = (ProxyMutex **) OPENSSL_malloc(CRYPTO_num_locks() * sizeof(ProxyMutex *));

  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    sslMutexArray[i] = new_ProxyMutex();
  }
  CRYPTO_set_locking_callback((void (*)(int, int, const char *, int)) sslLockingCallback);
  CRYPTO_set_id_callback(SSL_pthreads_thread_id);
}

int
SSLNetProcessor::reconfigure(void)
{
  int err = 0;

  cleanup();

  if (!open_ssl_initialized) {
    open_ssl_initialized = true;
    SSL_load_error_strings();
    SSL_library_init();
    initSSLLocks();
  }

  SslConfigParams *param = sslTerminationConfig.acquire();
  ink_assert(param);

  if (HttpProxyPort::hasSSL()) {
    // Only init server stuff if SSL is enabled in the config file
    sslCertLookup.init(param);
  }

  // Enable client regardless of config file setttings as remap file
  // can cause HTTP layer to connect using SSL. But only if SSL
  // initialization hasn't failed already.
  if (err == 0) {
    err = initSSLClient(param);
    if (err != 0)
      logSSLError("Can't initialize the SSL client, HTTPS in remap rules will not function");
  }

  sslTerminationConfig.release(param);
  return (err);
}

void
sslLockingCallback(int mode, int type, const char *file, int line)
{
  NOWARN_UNUSED(file);
  (void) line;
  if (mode & CRYPTO_LOCK) {
    MUTEX_TAKE_LOCK(ssl_NetProcessor.sslMutexArray[type], this_ethread());
  } else if (mode & CRYPTO_UNLOCK)
    MUTEX_UNTAKE_LOCK(ssl_NetProcessor.sslMutexArray[type], this_ethread());
  else
    ink_debug_assert(0);
}

unsigned long
SSL_pthreads_thread_id()
{
  EThread *eth = this_ethread();
  return (unsigned long) (eth->id);
}

void
SSLNetProcessor::logSSLError(const char *errStr, int critical)
{
  unsigned long l;
  char buf[256];
  const char *file, *data;
  int line, flags;
  unsigned long es;

  if (!critical) {
    if (errStr) {
      Debug("ssl_error", "SSL ERROR: %s.", errStr);
    } else {
      Debug("ssl_error", "SSL ERROR.");
    }
  } else {
    if (errStr) {
      Error("SSL ERROR: %s.", errStr);
    } else {
      Error("SSL ERROR.");
    }
  }

  es = CRYPTO_thread_id();
  while ((l = ERR_get_error_line_data(&file, &line, &data, &flags)) != 0) {
    if (!critical) {
      Debug("ssl_error", "SSL::%lu:%s:%s:%d:%s", es,
            ERR_error_string(l, buf), file, line, (flags & ERR_TXT_STRING) ? data : "");
    } else {
      Error("SSL::%lu:%s:%s:%d:%s", es, ERR_error_string(l, buf), file, line, (flags & ERR_TXT_STRING) ? data : "");
    }
  }
}

int
SSLNetProcessor::initSSLServerCTX(SSL_CTX * lCtx, const SslConfigParams * param,
    const char *serverCertPtr, const char *serverCaCertPtr,
    const char *serverKeyPtr)
{
  int session_id_context;
  int server_verify_client;
  char *completeServerCertPath;

  // disable selected protocols
  SSL_CTX_set_options(lCtx, param->ssl_ctx_options);

  switch (param->ssl_session_cache) {
  case SslConfigParams::SSL_SESSION_CACHE_MODE_OFF:
    SSL_CTX_set_session_cache_mode(lCtx, SSL_SESS_CACHE_OFF|SSL_SESS_CACHE_NO_INTERNAL);
    break;
  case SslConfigParams::SSL_SESSION_CACHE_MODE_SERVER:
    SSL_CTX_set_session_cache_mode(lCtx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_cache_size(lCtx, param->ssl_session_cache_size);
    break;
  }

  //might want to make configurable at some point.
  int verify_depth = param->verify_depth;
  SSL_CTX_set_quiet_shutdown(lCtx, 1);

  completeServerCertPath = Layout::relative_to (param->getServerCertPathOnly(), serverCertPtr);

  if (SSL_CTX_use_certificate_file(lCtx, completeServerCertPath, SSL_FILETYPE_PEM) <= 0) {
    Error ("SSL ERROR: Cannot use server certificate file: %s", completeServerCertPath);
    ats_free(completeServerCertPath);
    return -2;
  }
  if (serverCaCertPtr) {
    char *completeServerCaCertPath = Layout::relative_to (param->getServerCACertPathOnly(), serverCaCertPtr);
    if (SSL_CTX_add_extra_chain_cert_file(lCtx, completeServerCaCertPath) <= 0) {
      Error ("SSL ERROR: Cannot use server certificate chain file: %s", completeServerCaCertPath);
      ats_free(completeServerCaCertPath);
      return -2;
    }
    ats_free(completeServerCaCertPath);
  }

  if (serverKeyPtr == NULL)   // assume private key is contained in cert obtained from multicert file.
  {
    if (SSL_CTX_use_PrivateKey_file(lCtx, completeServerCertPath, SSL_FILETYPE_PEM) <= 0) {
      Error("SSL ERROR: Cannot use server private key file: %s", completeServerCertPath);
      ats_free(completeServerCertPath);
      return -3;
    }
  } else {
    if (param->getServerKeyPathOnly() != NULL) {
      char *completeServerKeyPath = Layout::get()->relative_to(param->getServerKeyPathOnly(), serverKeyPtr);
      if (SSL_CTX_use_PrivateKey_file(lCtx, completeServerKeyPath, SSL_FILETYPE_PEM) <= 0) {
        Error("SSL ERROR: Cannot use server private key file: %s", completeServerKeyPath);
        ats_free(completeServerKeyPath);
        return -3;
      }
      ats_free(completeServerKeyPath);
    } else {
      logSSLError("Empty ssl private key path in records.config.");
    }

  }
  ats_free(completeServerCertPath);

  if (!SSL_CTX_check_private_key(lCtx)) {
    logSSLError("Server private key does not match the certificate public key");
    return -4;
  }

  if (param->clientCertLevel != 0) {

    if (param->CACertFilename != NULL && param->CACertPath != NULL) {
      if ((!SSL_CTX_load_verify_locations(lCtx, param->CACertFilename, param->CACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(lCtx))) {
        logSSLError("CA Certificate file or CA Certificate path invalid");
        return -5;
      }
    }

    if (param->clientCertLevel == 2) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
    } else if (param->clientCertLevel == 1) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
    } else {
      // disable client cert support
      server_verify_client = SSL_VERIFY_NONE;
      Error("Illegal Client Certification Level in records.config\n");
    }

    session_id_context = 1;

    SSL_CTX_set_verify(lCtx, server_verify_client, NULL);
    SSL_CTX_set_verify_depth(lCtx, verify_depth);
    SSL_CTX_set_session_id_context(lCtx, (const unsigned char *) &session_id_context, sizeof session_id_context);

    SSL_CTX_set_client_CA_list(lCtx, SSL_load_client_CA_file(param->CACertFilename));
  }

  if (param->cipherSuite != NULL) {
    if (!SSL_CTX_set_cipher_list(lCtx, param->cipherSuite)) {
      logSSLError("Invalid Cipher Suite in records.config");
      return -6;
    }
  }

#if TS_USE_TLS_NPN
  SSL_CTX_set_next_protos_advertised_cb(lCtx,
      SSLNetVConnection::advertise_next_protocol, this);
#endif /* TS_USE_TLS_NPN */

  return 0;

}

int
SSLNetProcessor::initSSLClient(const SslConfigParams * param)
{
  ink_ssl_method_t meth = NULL;
  int client_verify_server;
  char *clientKeyPtr = NULL;

  // Note that we do not call RAND_seed() explicitly here, we depend on OpenSSL
  // to do the seeding of the PRNG for us. This is the case for all platforms that
  // has /dev/urandom for example.

  client_verify_server = param->clientVerify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
  meth = SSLv23_client_method();
  client_ctx = SSL_CTX_new(meth);

  // disable selected protocols
  SSL_CTX_set_options(client_ctx, param->ssl_ctx_options);
  int verify_depth = param->client_verify_depth;
  if (!client_ctx) {
    logSSLError("Cannot create new client contex.");
    return (-1);
  }
  // if no path is given for the client private key,
  // assume it is contained in the client certificate file.
  clientKeyPtr = param->clientKeyPath;
  if (clientKeyPtr == NULL)
    clientKeyPtr = param->clientCertPath;

  if (param->clientCertPath != 0) {
    if (SSL_CTX_use_certificate_file(client_ctx, param->clientCertPath, SSL_FILETYPE_PEM) <= 0) {
      Error ("SSL Error: Cannot use client certificate file: %s", param->clientCertPath);
      return (-2);
    }

    if (SSL_CTX_use_PrivateKey_file(client_ctx, clientKeyPtr, SSL_FILETYPE_PEM) <= 0) {
      Error ("SSL ERROR: Cannot use client private key file: %s", clientKeyPtr);
      return (-3);
    }

    if (!SSL_CTX_check_private_key(client_ctx)) {
      Error("SSL ERROR: Client private key (%s) does not match the certificate public key (%s)", clientKeyPtr, param->clientCertPath);
      return (-4);
    }
  }

  if (param->clientVerify) {
    SSL_CTX_set_verify(client_ctx, client_verify_server, NULL);
    /*???*/ SSL_CTX_set_verify_depth(client_ctx, verify_depth);
    // ???

    if (param->clientCACertFilename != NULL && param->clientCACertPath != NULL) {
      if ((!SSL_CTX_load_verify_locations(client_ctx, param->clientCACertFilename,
                                          param->clientCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(client_ctx))) {
        Error("SSL ERROR: Client CA Certificate file (%s) or CA Certificate path (%s) invalid", param->clientCACertFilename, param->clientCACertPath);
        return (-5);
      }
    }
  }
  return (0);
}

int
SSLNetProcessor::start(int number_of_ssl_threads)
{
  sslTerminationConfig.startup();
  int err = reconfigure();

  if (err != 0) {
    return -1;
  }

  if (number_of_ssl_threads < 1)
    return -1;

  SSLNetProcessor::ET_SSL = eventProcessor.spawn_event_threads(number_of_ssl_threads, "ET_SSL");
  if (err == 0) {
    err = UnixNetProcessor::start();
  }

  return err;
}

NetAccept *
SSLNetProcessor::createNetAccept()
{
  return ((NetAccept *) NEW(new SSLNetAccept));
}

// Virtual function allows etype to be upgraded to ET_SSL for SSLNetProcessor.  Does
// nothing for NetProcessor
void
SSLNetProcessor::upgradeEtype(EventType & etype)
{
  if (etype == ET_NET) {
    etype = ET_SSL;
  }
}

// Functions all THREAD_FREE and THREAD_ALLOC to be performed
// for both SSL and regular NetVConnection transparent to
// netProcessor connect functions. Yes it looks goofy to
// have them in both places, but it saves a bunch of
// connect code from being duplicated.
UnixNetVConnection *
SSLNetProcessor::allocateThread(EThread *t)
{
  return ((UnixNetVConnection *) THREAD_ALLOC(sslNetVCAllocator, t));
}

void
SSLNetProcessor::freeThread(UnixNetVConnection *vc, EThread *t)
{
  ink_assert(!vc->from_accept_thread);
  THREAD_FREE((SSLNetVConnection *) vc, sslNetVCAllocator, t);
}

SSLNetProcessor::SSLNetProcessor()
  : client_ctx(NULL), sslMutexArray(NULL)
{
}

SSLNetProcessor::~SSLNetProcessor()
{
  cleanup();
}
