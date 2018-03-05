/** @file

  Test crypto engine

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

/*
 * Test engine to exercise the asynchronous job interface
 * It performs the standard RSA operations, but for private key 
 * operations spawns a thread to sleep for 5 seconds before resuming
 * the asynchronous job
 */
#include <stdio.h>
#include <string.h>

#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <openssl/async.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/modes.h>
#include <pthread.h>
#include <unistd.h>


/* Engine Id and Name */
static const char *engine_id = "async-test";
static const char *engine_name = "Asynchronous test engine";


/* Engine Lifetime functions */
static int async_destroy(ENGINE *e);
static int engine_async_init(ENGINE *e);
static int async_finish(ENGINE *e);
void engine_load_async_int(void);


static void async_pause_job(void);

/* RSA */

static int async_pub_enc(int flen, const unsigned char *from,
                    unsigned char *to, RSA *rsa, int padding);
static int async_pub_dec(int flen, const unsigned char *from,
                    unsigned char *to, RSA *rsa, int padding);
static int async_rsa_priv_enc(int flen, const unsigned char *from,
                      unsigned char *to, RSA *rsa, int padding);
static int async_rsa_priv_dec(int flen, const unsigned char *from,
                      unsigned char *to, RSA *rsa, int padding);
static int async_rsa_mod_exp(BIGNUM *r0, const BIGNUM *I, RSA *rsa,
                              BN_CTX *ctx);

static int async_rsa_init(RSA *rsa);
static int async_rsa_finish(RSA *rsa);

static RSA_METHOD *async_rsa_method = NULL;

static int bind_async(ENGINE *e)
{
    /* Setup RSA_METHOD */
    if ((async_rsa_method = RSA_meth_new("Async RSA method", 0)) == NULL
        || RSA_meth_set_pub_enc(async_rsa_method, async_pub_enc) == 0
        || RSA_meth_set_pub_dec(async_rsa_method, async_pub_dec) == 0
        || RSA_meth_set_priv_enc(async_rsa_method, async_rsa_priv_enc) == 0
        || RSA_meth_set_priv_dec(async_rsa_method, async_rsa_priv_dec) == 0
        || RSA_meth_set_mod_exp(async_rsa_method, async_rsa_mod_exp) == 0
        || RSA_meth_set_bn_mod_exp(async_rsa_method, BN_mod_exp_mont) == 0
        || RSA_meth_set_init(async_rsa_method, async_rsa_init) == 0
        || RSA_meth_set_finish(async_rsa_method, async_rsa_finish) == 0) {
        fprintf(stderr, "Failed to initialize rsa method\n");
        return 0;
    }

    /* Ensure the dasync error handling is set up */
    ERR_load_ASYNC_strings();

    if (!ENGINE_set_id(e, engine_id)
        || !ENGINE_set_name(e, engine_name)
        || !ENGINE_set_RSA(e, async_rsa_method)
        || !ENGINE_set_destroy_function(e, async_destroy)
        || !ENGINE_set_init_function(e, engine_async_init)
        || !ENGINE_set_finish_function(e, async_finish)) {
        fprintf(stderr, "Failed to initialize\n");
        return 0;
    }

    return 1;
}

# ifndef OPENSSL_NO_DYNAMIC_ENGINE
static int bind_helper(ENGINE *e, const char *id)
{
    if (id && (strcmp(id, engine_id) != 0))
        return 0;
    if (!bind_async(e))
        return 0;
    return 1;
}

IMPLEMENT_DYNAMIC_CHECK_FN()
    IMPLEMENT_DYNAMIC_BIND_FN(bind_helper)
# endif

static ENGINE *engine_async(void)
{
    ENGINE *ret = ENGINE_new();
    if (!ret)
        return NULL;
    if (!bind_async(ret)) {
        ENGINE_free(ret);
        return NULL;
    }
    return ret;
}

void engine_load_async_int(void)
{
    ENGINE *toadd = engine_async();
    if (!toadd)
        return;
    ENGINE_add(toadd);
    ENGINE_free(toadd);
    ERR_clear_error();
}

static int engine_async_init(ENGINE *e)
{
    return 1;
}


static int async_finish(ENGINE *e)
{
    return 1;
}


static int async_destroy(ENGINE *e)
{
    RSA_meth_free(async_rsa_method);
    return 1;
}

static void wait_cleanup(ASYNC_WAIT_CTX *ctx, const void *key,
                         OSSL_ASYNC_FD readfd, void *pvwritefd)
{
    OSSL_ASYNC_FD *pwritefd = (OSSL_ASYNC_FD *)pvwritefd;
    close(readfd);
    close(*pwritefd);
    OPENSSL_free(pwritefd);
}

#define DUMMY_CHAR 'X'

static void async_pause_job(void) {
    ASYNC_JOB *job;
    ASYNC_WAIT_CTX *waitctx;
    OSSL_ASYNC_FD pipefds[2] = {0, 0};
    OSSL_ASYNC_FD *writefd;
    char buf = DUMMY_CHAR;

    if ((job = ASYNC_get_current_job()) == NULL)
        return;

    waitctx = ASYNC_get_wait_ctx(job);

    if (ASYNC_WAIT_CTX_get_fd(waitctx, engine_id, &pipefds[0],
                              (void **)&writefd)) {
        pipefds[1] = *writefd;
    } else {
        writefd = (OSSL_ASYNC_FD *)OPENSSL_malloc(sizeof(*writefd));
        if (writefd == NULL)
            return;
        if (pipe(pipefds) != 0) {
            OPENSSL_free(writefd);
            return;
        }
        *writefd = pipefds[1];

        if(!ASYNC_WAIT_CTX_set_wait_fd(waitctx, engine_id, pipefds[0],
                                       writefd, wait_cleanup)) {
            wait_cleanup(waitctx, engine_id, pipefds[0], writefd);
            return;
        }
    }

    /* Ignore errors - we carry on anyway */
    ASYNC_pause_job();

    /* Clear the wake signal */
    if (read(pipefds[0], &buf, 1) < 0)
        return;
}

void *
delay_method(void *arg)
{
  int signal_fd = (intptr_t)arg;
  sleep(5);
  uint64_t buf = 1;
  write(signal_fd, &buf, sizeof(buf));
}


void
spawn_delay_thread()
{
  pthread_t thread_id;
  OSSL_ASYNC_FD signal_fd;
  OSSL_ASYNC_FD pipefds[2] = {0, 0};
  ASYNC_JOB *job;
  if ((job = ASYNC_get_current_job()) == NULL)
      return;

  ASYNC_WAIT_CTX *waitctx = ASYNC_get_wait_ctx(job);

  size_t numfds;
  if (ASYNC_WAIT_CTX_get_all_fds(waitctx, &signal_fd, &numfds) && numfds > 0) {
  } else {
    OSSL_ASYNC_FD pipefds[2] = {0,0};
    OSSL_ASYNC_FD *writefd = OPENSSL_malloc(sizeof(*writefd));
    pipe(pipefds); 
    signal_fd = *writefd = pipefds[1];
    ASYNC_WAIT_CTX_set_wait_fd(waitctx, engine_id, pipefds[0], writefd, wait_cleanup);
  }
  
  pthread_create(&thread_id, NULL, delay_method, (void *)((intptr_t)signal_fd));
}


/*
 * RSA implementation
 */

static int async_pub_enc(int flen, const unsigned char *from,
                    unsigned char *to, RSA *rsa, int padding) {
    return RSA_meth_get_pub_enc(RSA_PKCS1_OpenSSL())
        (flen, from, to, rsa, padding);
}

static int async_pub_dec(int flen, const unsigned char *from,
                    unsigned char *to, RSA *rsa, int padding) {
    return RSA_meth_get_pub_dec(RSA_PKCS1_OpenSSL())
        (flen, from, to, rsa, padding);
}

static int async_rsa_priv_enc(int flen, const unsigned char *from,
                      unsigned char *to, RSA *rsa, int padding)
{
    //printf("async_priv_enc\n");
    spawn_delay_thread();
    async_pause_job();
    return RSA_meth_get_priv_enc(RSA_PKCS1_OpenSSL())
        (flen, from, to, rsa, padding);
}

static int async_rsa_priv_dec(int flen, const unsigned char *from,
                      unsigned char *to, RSA *rsa, int padding)
{
    //printf("async_priv_dec\n");
    spawn_delay_thread();
    async_pause_job();
    return RSA_meth_get_priv_dec(RSA_PKCS1_OpenSSL())
        (flen, from, to, rsa, padding);
}

static int async_rsa_mod_exp(BIGNUM *r0, const BIGNUM *I, RSA *rsa, BN_CTX *ctx)
{
    return RSA_meth_get_mod_exp(RSA_PKCS1_OpenSSL())(r0, I, rsa, ctx);
}

static int async_rsa_init(RSA *rsa)
{
    return RSA_meth_get_init(RSA_PKCS1_OpenSSL())(rsa);
}
static int async_rsa_finish(RSA *rsa)
{
    return RSA_meth_get_finish(RSA_PKCS1_OpenSSL())(rsa);
}

