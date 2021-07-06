/** @file

  SSL delay test server

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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <openssl/err.h>
#include <sys/time.h>
#include <sys/select.h>
#include <cerrno>

char req_buf[10000];
char post_buf[1000];

SSL_CTX *svr_ctx;
int connect_delay;
int ttfb_delay;

pthread_mutex_t *mutex_buf = nullptr;

struct thread_info {
  struct addrinfo *result, *rp;
  SSL_SESSION *session;
};

void
SSL_locking_callback(int mode, int type, const char *file, int line)
{
  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&mutex_buf[type]);
  } else if (mode & CRYPTO_UNLOCK) {
    pthread_mutex_unlock(&mutex_buf[type]);
  } else {
    printf("invalid SSL locking mode 0x%x\n", mode);
  }
}

void
SSL_pthreads_thread_id(CRYPTO_THREADID *id)
{
  CRYPTO_THREADID_set_numeric(id, (unsigned long)pthread_self());
}

char input_buf[1000];
char response_buf[] = "200 HTTP/1.1\r\nConnection: close\r\n\r\n";

void *
run_session(void *arg)
{
  int sfd  = (intptr_t)arg;
  SSL *ssl = SSL_new(svr_ctx);
  if (ssl == nullptr) {
    fprintf(stderr, "Failed to create ssl\n");
    return nullptr;
  }

#if OPENSSL_VERSION_NUMBER >= 0x10100000
  SSL_set_max_proto_version(ssl, TLS1_2_VERSION);
#endif

  SSL_set_fd(ssl, sfd);

  fprintf(stderr, "Accept try %d\n", sfd);

  // Potentially delay before processing the TLS handshake
  if (connect_delay > 0) {
    fprintf(stderr, "Connect delay %d\n", connect_delay);
    // Delay here
    sleep(connect_delay);
  }
  int did_accept = 0;

  int ret = SSL_accept(ssl);

  if (!did_accept && ret == 1) {
    did_accept = 1;
    fprintf(stderr, "Done accept\n");
  } else {
    int ssl_err = SSL_get_error(ssl, ret);
    fprintf(stderr, "Failed accept ssl_error=%d errno=%d\n", ssl_err, errno);
    return nullptr;
  }

  if (did_accept) {
    ret = SSL_read(ssl, input_buf, sizeof(input_buf));
    if (ret < 0) {
      // Failure
      fprintf(stderr, "Server read failure\n");
    } else {
      fprintf(stderr, "TTFB delay\n");
      if (ttfb_delay > 0) {
        // TTFB delay
        sleep(ttfb_delay);
      }
      fprintf(stderr, "Write response\n");
      ret = SSL_write(ssl, response_buf, strlen(response_buf));
      fprintf(stderr, "Write response %d\n", ret);
      if (ret <= 0) {
        fprintf(stderr, "Server write failure\n");
      } else {
        fprintf(stderr, "Write response succeeded.  Go to the next one\n");
      }
    }
  }
  if (ssl) {
    SSL_free(ssl);
  }
  close(sfd);

  return nullptr;
}

/**
 * Simple TLS server with configurable delays
 */
int
main(int argc, char *argv[])
{
  int sfd;

  if (argc != 5) {
    fprintf(stderr, "Usage: %s <listen port> <handshake delay> <ttfb delay> <cert/key pem file>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  short listen_port    = atoi(argv[1]);
  connect_delay        = atoi(argv[2]);
  ttfb_delay           = atoi(argv[3]);
  const char *pem_file = argv[4];

  fprintf(stderr, "Listen on %d connect delay=%d ttfb delay=%d\n", listen_port, connect_delay, ttfb_delay);

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in serv_addr;

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port        = htons(listen_port);

  bind(listenfd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));

  SSL_load_error_strings();
  SSL_library_init();

  mutex_buf = static_cast<pthread_mutex_t *>(OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t)));
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_mutex_init(&mutex_buf[i], nullptr);
  }

  CRYPTO_set_locking_callback(SSL_locking_callback);
#if CRYPTO_THREADID_set_callback
  CRYPTO_THREADID_set_callback(SSL_pthreads_thread_id);
#endif

  svr_ctx = SSL_CTX_new(SSLv23_server_method());
  // Associate cert and key
  if (SSL_CTX_use_certificate_file(svr_ctx, pem_file, SSL_FILETYPE_PEM) != 1) {
    printf("Failed to load certificate from %s\n", pem_file);
    exit(1);
  }

  if (SSL_CTX_use_PrivateKey_file(svr_ctx, pem_file, SSL_FILETYPE_PEM) != 1) {
    printf("Failed to load private key from %s\n", pem_file);
    exit(1);
  }

  listen(listenfd, 10);

  for (;;) {
    sfd = accept(listenfd, (struct sockaddr *)nullptr, nullptr);
    if (sfd <= 0) {
      // Failure
      printf("Listen failure\n");
      exit(1);
    }

    fprintf(stderr, "Spawn off new sesson thread %d\n", sfd);

    // Spawn off new thread
    pthread_t thread_id;
    pthread_create(&thread_id, nullptr, run_session, (void *)(static_cast<intptr_t>(sfd)));
  }

  exit(0);
}
