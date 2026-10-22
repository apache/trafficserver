/** @file

  smuggle_client.c

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
#include <netinet/tcp.h>
#include <assert.h>

const char *req_and_post_buf =
  "GET / HTTP/1.1\r\nConnection: close\r\nHost: foo.com\r\nTransfer-Encoding: chunked\r\nContent-Length: 301\r\n\r\n0\r\n\r\nPOST "
  "http://sneaky.com/ HTTP/1.1\r\nContent-Length: 10\r\nConnection: close\r\nX-Foo: Z\r\n\r\n1234567890";

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
  int sfd = -1, s;

  if (argc < 3) {
    fprintf(stderr, "Usage: %s <target addr> <target_port>\n", argv[0]);
    exit(1);
  }

  const char *target      = argv[1];
  const char *target_port = argv[2];
  printf("using address: %s and port: %s\n", target, target_port);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_library_init();
#else
  OPENSSL_init_ssl(0, NULL);
#endif

  /* Obtain address(es) matching host/port */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family   = AF_UNSPEC;   /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
  hints.ai_flags    = 0;
  hints.ai_protocol = 0; /* Any protocol */

  s = getaddrinfo(target, target_port, &hints, &result);
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

  SSL_CTX *client_ctx = SSL_CTX_new(SSLv23_client_method());
  assert(client_ctx != NULL);
  SSL *ssl = SSL_new(client_ctx);
  assert(ssl != NULL);

  SSL_set_fd(ssl, sfd);
  int ret = SSL_connect(ssl);
  assert(ret == 1);

  printf("Send request\n");
  if ((ret = SSL_write(ssl, req_and_post_buf, strlen(req_and_post_buf))) <= 0) {
    int error = SSL_get_error(ssl, ret);
    printf("SSL_write failed %d", error);
    exit(1);
  }

  int read_bytes;
  do {
    char input_buf[1024];
    read_bytes = SSL_read(ssl, input_buf, sizeof(input_buf) - 1);
    if (read_bytes > 0) {
      input_buf[read_bytes] = '\0';
      printf("Received %d bytes %s\n", read_bytes, input_buf);
    }
  } while (read_bytes > 0);

  close(sfd);

  exit(0);
}
