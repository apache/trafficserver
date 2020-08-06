/** @file

  SSL post test client

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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>

#define NUM_THREADS 10

char req_buf[10000];
char post_buf[1000];

pthread_mutex_t *mutex_buf = NULL;

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

// In OpenSSL 1.1, locking and thread id logic was changed and the
// CRYPTO_THREADID_set_callback function became a macro defined to be 0. In
// later versions, therefore, static analysis tools flag the use of this as a
// problem. Thus in order to see whether CRYPTO_THREADID_set_callback is a
// valid function we check that it is not a defined macro.
#if !defined(CRYPTO_THREADID_set_callback)
void
SSL_pthreads_thread_id(CRYPTO_THREADID *id)
{
  CRYPTO_THREADID_set_numeric(id, (unsigned long)pthread_self());
}
#endif

void *
spawn_same_session_send(void *arg)
{
  struct thread_info *tinfo = (struct thread_info *)arg;

  // Start again, but with the session set this time
  int sfd = socket(tinfo->rp->ai_family, tinfo->rp->ai_socktype, tinfo->rp->ai_protocol);
  if (sfd == -1) {
    printf("Failed to get socket");
    perror("Failed");
    pthread_exit((void *)1);
  }
  if (connect(sfd, tinfo->rp->ai_addr, tinfo->rp->ai_addrlen) < 0) {
    printf("Failed to connect %d\n", sfd);
    perror("Failed");
    pthread_exit((void *)1);
  }

  fcntl(sfd, F_SETFL, O_NONBLOCK);
  // Make sure we are nagling
  int one = 0;
  setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  SSL_CTX *client_ctx = SSL_CTX_new(SSLv23_client_method());
  SSL *ssl            = SSL_new(client_ctx);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  SSL_set_max_proto_version(ssl, TLS1_2_VERSION);
#endif

  SSL_set_session(ssl, tinfo->session);

  SSL_set_fd(ssl, sfd);
  int ret            = SSL_connect(ssl);
  int read_count     = 0;
  int write_count    = 1;
  int write_ret      = -1;
  int post_write_ret = -1;

  while (ret < 0) {
    int error = SSL_get_error(ssl, ret);
    fd_set reads;
    fd_set writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    switch (error) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_ACCEPT:
      FD_SET(sfd, &reads);
      read_count++;
      break;
    case SSL_ERROR_WANT_CONNECT:
    case SSL_ERROR_WANT_WRITE:
      FD_SET(sfd, &writes);
      write_count++;
      break;
    case SSL_ERROR_SYSCALL:
    case SSL_ERROR_SSL:
    case SSL_ERROR_ZERO_RETURN:
      printf("Error %d\n", error);
      pthread_exit((void *)1);
      break;
    default:
      // printf("Unknown error is %d", error);
      FD_SET(sfd, &reads);
      FD_SET(sfd, &writes);
      break;
    }
    ret = select(sfd + 1, &reads, &writes, NULL, NULL);
    if (FD_ISSET(sfd, &reads) || FD_ISSET(sfd, &writes)) {
      ret = write_ret = SSL_write(ssl, req_buf, strlen(req_buf));
      if (write_ret > 0) {
        post_write_ret = SSL_write(ssl, post_buf, sizeof(post_buf));
      }
    }
  }

  while (write_ret <= 0) {
    write_ret = SSL_write(ssl, req_buf, strlen(req_buf));
  }
  while (post_write_ret <= 0) {
    post_write_ret = SSL_write(ssl, post_buf, sizeof(post_buf));
  }

  // Have to do the shutdown so the data packet is sent out fast enough
  // so it might be read with the last handshake packet
  shutdown(sfd, SHUT_WR);

  char input_buf[1024];
  int read_bytes = SSL_read(ssl, input_buf, sizeof(input_buf));
  int total_read = 0;
  while (read_bytes != 0) {
    fd_set reads;
    fd_set writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    if (read_bytes > 0) {
      total_read += read_bytes;
      FD_SET(sfd, &reads);
    } else {
      int error = SSL_get_error(ssl, read_bytes);
      switch (error) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_ACCEPT:
        FD_SET(sfd, &reads);
        break;
      case SSL_ERROR_WANT_CONNECT:
      case SSL_ERROR_WANT_WRITE:
        printf("Unexpected write\n");
        pthread_exit((void *)1);
        break;
      case SSL_ERROR_SYSCALL:
      case SSL_ERROR_SSL:
      case SSL_ERROR_ZERO_RETURN:
        printf("Error Read\n");
        pthread_exit((void *)1);
        break;
      default:
        FD_SET(sfd, &reads);
        FD_SET(sfd, &writes);
        break;
      }
    }
    select(sfd + 1, &reads, &writes, NULL, NULL);
    if (FD_ISSET(sfd, &reads)) {
      read_bytes = SSL_read(ssl, input_buf, sizeof(input_buf));
    }
  }
  if (read_bytes > 0 && read_bytes < 1024) {
    input_buf[read_bytes] = '\0';
  } else {
    input_buf[1023] = '\0';
  }
  // printf("total_bytes=%d Received bytes=%d handshake writes=%d handshake reads=%d\n", total_read, read_bytes, write_count,
  // read_count);

  //  Leaking the socket, so that the EOS does not wake up a potentially
  //  stalled ATS connection.  Want to wait for the inactivity timeout
  //  to make it clear that there was a stalling problem
  // close(sfd);
  pthread_exit(NULL);
}

/**
 * Connect to a server.
 * Handshake
 * Exit immediatesly
 */
int
main(int argc, char *argv[])
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s;

  if (argc < 4) {
    fprintf(stderr, "Usage: %s host thread-count header-count [port]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  char *host       = argv[1];
  int header_count = atoi(argv[3]);
  snprintf(req_buf, sizeof(req_buf), "POST /post HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-length:%zu\r\n", host,
           sizeof(post_buf));
  int i;
  for (i = 0; i < header_count; i++) {
    sprintf(req_buf + strlen(req_buf), "header%d:%d\r\n", i, i);
  }
  strcat(req_buf, "\r\n");
  memset(post_buf, '0', sizeof(post_buf));

  int thread_count = atoi(argv[2]);

  const char *port = argc == 5 ? argv[4] : "443";

  /* Obtain address(es) matching host/port */

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family   = AF_UNSPEC;   /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
  hints.ai_flags    = 0;
  hints.ai_protocol = 0; /* Any protocol */

  s = getaddrinfo(host, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }

  /* getaddrinfo() returns a list of address structures.
   * Try each address until we successfully connect(2).
   * socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1) {
      continue;
    }
    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
      break; /* Success */
    }

    close(sfd);
  }

  if (rp == NULL) { /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    exit(EXIT_FAILURE);
  }

  // fcntl(sfd, F_SETFL, O_NONBLOCK);

  SSL_load_error_strings();
  SSL_library_init();

  mutex_buf = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
  for (i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_mutex_init(&mutex_buf[i], NULL);
  }

  CRYPTO_set_locking_callback(SSL_locking_callback);

// See the '!defined(CRYPTO_THREADID_set_callback)' comment above for why we
// test for !defined here.
#if !defined(CRYPTO_THREADID_set_callback)
  CRYPTO_THREADID_set_callback(SSL_pthreads_thread_id);
#endif

  SSL_CTX *client_ctx = SSL_CTX_new(SSLv23_client_method());
  SSL *ssl            = SSL_new(client_ctx);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  SSL_set_max_proto_version(ssl, TLS1_2_VERSION);
#endif

  SSL_set_fd(ssl, sfd);
  int ret = SSL_connect(ssl);

  if (ret <= 0) {
    int error = SSL_get_error(ssl, ret);
    printf("SSL_connect failed %d", error);
    exit(1);
  }

  printf("Sent request\n");
  if ((ret = SSL_write(ssl, req_buf, strlen(req_buf))) <= 0) {
    int error = SSL_get_error(ssl, ret);
    printf("SSL_write failed %d", error);
    exit(1);
  }
  SSL_write(ssl, post_buf, sizeof(post_buf));

  char input_buf[1024];
  int read_bytes = SSL_read(ssl, input_buf, sizeof(input_buf));
  if (read_bytes > 0 && read_bytes < 1024) {
    input_buf[read_bytes] = '\0';
  } else {
    input_buf[1023] = '\0';
  }
  // printf("Received %d bytes %s\n", read_bytes, input_buf);
  SSL_SESSION *session = SSL_get_session(ssl);
  close(sfd);
  struct thread_info tinfo;
  tinfo.rp           = rp;
  tinfo.session      = session;
  pthread_t *threads = (pthread_t *)malloc(thread_count * sizeof(pthread_t));
  for (i = 0; i < thread_count; i++) {
    pthread_create(threads + i, NULL, spawn_same_session_send, &tinfo);
  }

  void *retval;
  for (i = 0; i < thread_count; i++) {
    retval = NULL;
    pthread_join(threads[i], &retval);
    if (retval != NULL) {
      printf("Thread %d failed %p\n", i, retval);
    }
  }

  printf("All threads finished\n");

  exit(0);
}
