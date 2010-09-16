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

   SSLNet.cc --

   Description: Common SSL initialization/cleanup fuctions from SSLNet.h
 ****************************************************************************/

#include "ink_config.h"

#include "P_Net.h"
#if !defined (_IOCORE_WIN32)    // remove when NT openssl lib is upgraded to eng-0.9.6
#include "openssl/engine.h"
#include "openssl/dso.h"
#endif

void sslLockingCallback(int mode, int type, const char *file, int line);
unsigned long SSL_pthreads_thread_id();

bool SSLNetProcessor::open_ssl_initialized = false;


int
SSL_CTX_add_extra_chain_cert_file(SSL_CTX * ctx, const char *file)
{
  BIO *in;
  int j;
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

  j = ERR_R_PEM_LIB;
  x = PEM_read_bio_X509(in, NULL, ctx->default_passwd_callback, ctx->default_passwd_callback_userdata);
  if (x == NULL) {
    SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, j);
    goto end;
  }

  ret = SSL_CTX_add_extra_chain_cert(ctx, x);
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
#if !defined (_IOCORE_WIN32)
    OPENSSL_free(sslMutexArray);
#else
    Free(sslMutexArray);
#endif
    sslMutexArray = NULL;
  }

  if (ctx)
    SSL_CTX_free(ctx);
  ctx = NULL;
  if (client_ctx)
    SSL_CTX_free(client_ctx);
  client_ctx = NULL;
}

void
SSLNetProcessor::initSSLLocks(void)
{

#if !defined (_IOCORE_WIN32)
  sslMutexArray = (ProxyMutex **) OPENSSL_malloc(CRYPTO_num_locks() * sizeof(ProxyMutex *));
#else
  sslMutexArray = (ProxyMutex **) Malloc(CRYPTO_num_locks() * sizeof(ProxyMutex *));
#endif

  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    sslMutexArray[i] = new_ProxyMutex();
  }
  CRYPTO_set_locking_callback((void (*)(int, int, const char *, int)) sslLockingCallback);
  CRYPTO_set_id_callback(SSL_pthreads_thread_id);
}

int
SSLNetProcessor::reconfigure(void)
{
  int ssl_mode = SslConfigParams::SSL_TERM_MODE_NONE, err = 0;
  int sslServerEnabled = 0;

  cleanup();

  if (!open_ssl_initialized) {
    open_ssl_initialized = true;
    SSL_load_error_strings();
    SSLeay_add_ssl_algorithms();
    initSSLLocks();
  }

  SslConfigParams *param = sslTerminationConfig.acquire();
  ink_assert(param);

  ssl_mode = param->getTerminationMode();
  sslServerEnabled = ssl_mode & SslConfigParams::SSL_TERM_MODE_CLIENT;

  if (sslServerEnabled) {
    // Only init server stuff if SSL is enabled in the config file
    err = initSSL(param);
    if (err == 0) {
      sslCertLookup.init(param);
    } else {
      logSSLError("Can't initialize the SSL library, disabling SSL termination!");
      sslTerminationConfig.clearTermEnabled();
    }
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
SSLNetProcessor::initSSL(SslConfigParams * param)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD now
  const SSL_METHOD *meth = NULL;
#else
  SSL_METHOD *meth = NULL;
#endif
  char *serverKeyPtr = NULL;

  int randBuff[64];
  int irand;
  for (irand = 8; irand < 32; irand++) {
    // coverity[secure_coding]
    randBuff[irand] += rand();
  }

  long *rbp = (long *) randBuff;
  *rbp++ += (long) serverKeyPtr;
  *rbp++ ^= *((long *) (&meth));
  srand((unsigned) time(NULL));

  for (irand = 32; irand < 64; irand++)
    randBuff[irand] ^= this_ethread()->generator.random();

  RAND_seed((void *) randBuff, sizeof(randBuff));


  accept_port_number = param->ssl_accept_port_number;

  if ((unsigned int) accept_port_number >= 0xFFFF) {
    Error("\ncannot listen on port %d.\naccept port cannot be larger that 65535.\n"
                        "please check your Traffic Server configurations", accept_port_number);
    return (1);
  }
  // these are not operating system specific calls and
  // this ifdef needs to be removed when the openssl-engine
  // library is built for NT.
#if !defined (_IOCORE_WIN32)

#ifdef USE_OPENSSL_ENGINES
  int engineType;
  char *accelLibPath;

  ENGINE *accleratorEng;

  ENGINE_load_builtin_engines();
  engineType = param->sslAccelerator;

  if (engineType == SSL_NCIPHER_ACCEL) {
    accelLibPath = param->ncipherAccelLibPath;
    accleratorEng = ENGINE_by_id("chil");
  } else if (engineType == SSL_CSWIFT_ACCEL) {
    accelLibPath = param->cswiftAccelLibPath;
    accleratorEng = ENGINE_by_id("cswift");
  } else if (engineType == SSL_ATALLA_ACCEL) {
    accelLibPath = param->cswiftAccelLibPath;
    accleratorEng = ENGINE_by_id("atalla");
  } else if (engineType == SSL_BROADCOM_ACCEL) {
    accelLibPath = param->broadcomAccelLibPath;
    accleratorEng = ENGINE_by_id("ubsec");
  } else {
    accelLibPath = NULL;
    accleratorEng = ENGINE_by_id("dynamic");
  }

  if (accleratorEng == NULL) {
    logSSLError("Cannot find SSL hardware accelerator library, SSL will operate without accelerator.");
    return (-5);
  }

  if (accelLibPath != NULL) {
    // Removed, not sure what it does, or how... it's nowhere to be found in OpenSSL /leif.
    // DSO_set_dl_path (accelLibPath);
  }

  if (!ENGINE_set_default(accleratorEng, ENGINE_METHOD_ALL)) {
    logSSLError("Cannot set SSL hardware accelerator card as default, SSL will operate without accelerator.");
    logSSLError
      ("Check records.config variable for SSL accelerator hardware library path in the SSL Termination section. ");

    // set software engine as default
    ENGINE_set_default(ENGINE_by_id("openssl"), ENGINE_METHOD_ALL);
  }

  ENGINE_free(accleratorEng);

#endif // ! USE_OPENSSL_ENGINES

#endif // !_IOCORE_WIN32

  meth = SSLv23_server_method();
  ctx = SSL_CTX_new(meth);
  if (!ctx) {
    logSSLError("Cannot create new server contex.");
    return (-1);
  }
  // if no path is given for the server private key,
  // assume it is contained in the server certificate file.
/*  serverKeyPtr = param->serverKeyPath;
  if (serverKeyPtr == NULL)
	  serverKeyPtr = param->serverCertPath;
*/
  return (initSSLServerCTX(param, ctx, param->serverCertPath, param->serverKeyPath, true));

/*
  verify_depth = param->verify_depth;

  // if no path is given for the server private key,
  // assume it is contained in the server certificate file.
  serverKeyPtr = param->serverKeyPath;
  if (serverKeyPtr == NULL)
	  serverKeyPtr = param->serverCertPath;

  if (SSL_CTX_use_certificate_file(ctx, param->serverCertPath, SSL_FILETYPE_PEM) <= 0)
  {
    logSSLError("Cannot use server certificate file");
    return(-2);
  }

  if(SSL_CTX_use_PrivateKey_file(ctx, serverKeyPtr, SSL_FILETYPE_PEM) <= 0)
  {
    logSSLError("Cannot use server private key file");
    return(-3);
  }

  if(!SSL_CTX_check_private_key(ctx))
  {
    logSSLError("Server private key does not match the certificate public key");
    return(-4);
  }


  if(param->clientCertLevel != 0)
  {

	if (param->CACertFilename != NULL && param->CACertPath != NULL)
	{
	  if ((!SSL_CTX_load_verify_locations(ctx, param->CACertFilename, param->CACertPath)) ||
		(!SSL_CTX_set_default_verify_paths(ctx)))
	  {
		logSSLError("CA Certificate file or CA Certificate path invalid");
		return(-5);
	  }
	}

	if(param->clientCertLevel == 2)
	  server_verify_client =    SSL_VERIFY_PEER |
                                SSL_VERIFY_FAIL_IF_NO_PEER_CERT |
								SSL_VERIFY_CLIENT_ONCE;
	else if(param->clientCertLevel == 1)
	        server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
	else // disable client cert support
	{
	  server_verify_client=SSL_VERIFY_NONE;
	   Error("Illegal Client Certification Level in records.config\n");
	}

	session_id_context = 1;

	SSL_CTX_set_verify(ctx,server_verify_client, verify_callback);
	SSL_CTX_set_verify_depth(ctx, verify_depth);
	SSL_CTX_set_session_id_context(ctx,(const unsigned char *)&session_id_context,
		sizeof session_id_context);

	SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(param->CACertFilename));
  }
  return(0);
*/
}

int
SSLNetProcessor::initSSLServerCTX(SslConfigParams * param, SSL_CTX * lCtx,
                                  char *serverCertPtr, char *serverKeyPtr, bool defaultEnabled)
{
  int session_id_context;
  int server_verify_client;
  char *completeServerCertPath;

  // disable selected protocols
  SSL_CTX_set_options(lCtx, param->ssl_ctx_options);

  //might want to make configurable at some point.
  verify_depth = param->verify_depth;
  SSL_CTX_set_quiet_shutdown(lCtx, 1);

  if (defaultEnabled) {
    if (SSL_CTX_use_certificate_file(lCtx, param->serverCertPath, SSL_FILETYPE_PEM) <= 0) {
      logSSLError("Cannot use server certificate file");
      return (-2);
    }
    if (param->serverKeyPath != NULL) {
      if (SSL_CTX_use_PrivateKey_file(lCtx, param->serverKeyPath, SSL_FILETYPE_PEM) <= 0) {
        logSSLError("Cannot use server private key file");
        return (-3);
      }
    } else                      // assume key is contained in the cert file.
    {
      if (SSL_CTX_use_PrivateKey_file(lCtx, param->serverCertPath, SSL_FILETYPE_PEM) <= 0) {
        logSSLError("Cannot use server private key file");
        return (-3);
      }
    }

    if (param->serverCertChainPath) {
      if (SSL_CTX_add_extra_chain_cert_file(lCtx, param->serverCertChainPath) <= 0) {
        logSSLError("Cannot use server certificate chain file");
        return (-2);
      }
    }
  } else {
    const size_t completeServerCertPathSize = strlen(param->getServerCertPathOnly()) + strlen(serverCertPtr) + 1;
    completeServerCertPath = (char *) xmalloc(completeServerCertPathSize);

    ink_strncpy(completeServerCertPath, (const char *) param->getServerCertPathOnly(), completeServerCertPathSize);
    ink_strlcat(completeServerCertPath, serverCertPtr, completeServerCertPathSize);
    if (SSL_CTX_use_certificate_file(lCtx, completeServerCertPath, SSL_FILETYPE_PEM) <= 0) {
      logSSLError("Cannot use server certificate file");
      return (-2);
    }

    if (serverKeyPtr == NULL)   // assume private key is contained in cert obtained from multicert file.
    {
      if (SSL_CTX_use_PrivateKey_file(lCtx, completeServerCertPath, SSL_FILETYPE_PEM) <= 0) {
        logSSLError("Cannot use server private key file");
        return (-3);
      }
    } else {
      if (param->getServerKeyPathOnly() != NULL) {
        if (SSL_CTX_use_PrivateKey_file(lCtx, serverKeyPtr, SSL_FILETYPE_PEM) <= 0) {
          logSSLError("Cannot use server private key file");
          return (-3);
        }
      } else {
        logSSLError("Empty ssl private key path in records.config.");
      }

    }


  }

  if (!SSL_CTX_check_private_key(lCtx)) {
    logSSLError("Server private key does not match the certificate public key");
    return (-4);
  }


  if (param->clientCertLevel != 0) {

    if (param->CACertFilename != NULL && param->CACertPath != NULL) {
      if ((!SSL_CTX_load_verify_locations(lCtx, param->CACertFilename, param->CACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(lCtx))) {
        logSSLError("CA Certificate file or CA Certificate path invalid");
        return (-5);
      }
    }

    if (param->clientCertLevel == 2)
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
    else if (param->clientCertLevel == 1)
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
    else                        // disable client cert support
    {
      server_verify_client = SSL_VERIFY_NONE;
      Error("Illegal Client Certification Level in records.config\n");
    }

    session_id_context = 1;

    SSL_CTX_set_verify(lCtx, server_verify_client, NULL);
    SSL_CTX_set_verify_depth(lCtx, verify_depth);
    SSL_CTX_set_session_id_context(lCtx, (const unsigned char *) &session_id_context, sizeof session_id_context);

    SSL_CTX_set_client_CA_list(lCtx, SSL_load_client_CA_file(param->CACertFilename));
  }
  return (0);

}

int
SSLNetProcessor::initSSLClient(SslConfigParams * param)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD now
  const SSL_METHOD *meth = NULL;
#else
  SSL_METHOD *meth = NULL;
#endif
  int client_verify_server;
  char *clientKeyPtr = NULL;

  int randBuff[128];
  int irand;
  for (irand = 8; irand < 64; irand++) {
    // coverity[secure_coding]
    randBuff[irand] += rand();
  }

  long *rbp = (long *) randBuff;
  *rbp++ += (long) clientKeyPtr;
  *rbp++ ^= *((long *) (&meth));
  srand((unsigned) time(NULL));

  for (irand = 64; irand < 128; irand++)
    randBuff[irand] ^= this_ethread()->generator.random();

  RAND_seed((void *) randBuff, sizeof(randBuff));

  client_verify_server = param->clientVerify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;


  meth = SSLv23_client_method();
  client_ctx = SSL_CTX_new(meth);

  // disable selected protocols
  SSL_CTX_set_options(client_ctx, param->ssl_ctx_options);
  verify_depth = param->client_verify_depth;
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
      logSSLError("Cannot use client certificate file");
      return (-2);
    }

    if (SSL_CTX_use_PrivateKey_file(client_ctx, clientKeyPtr, SSL_FILETYPE_PEM) <= 0) {
      logSSLError("Cannot use client private key file");
      return (-3);
    }

    if (!SSL_CTX_check_private_key(client_ctx)) {
      logSSLError("Client private key does not match the certificate public key");
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
        logSSLError("Client CA Certificate file or CA Certificate path invalid");
        return (-5);
      }
    }
  }
  return (0);
}
