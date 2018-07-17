/** @file

  POST test server

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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

const char *response_buffer = "HTTP/1.1 401 Auth Needed\r\nHost:example.com\r\nContent-length:0\r\n\r\n";
int
main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <listen port>\n", argv[0]);
    exit(1);
  }
  printf("Starting...\n");

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in serv_addr, peer_addr;
  socklen_t peer_len;

  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  int port                  = atoi(argv[1]);
  serv_addr.sin_port        = htons(port);
  bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  listen(listenfd, 10);

  for (;;) {
    socklen_t client_len;
    struct sockaddr_in client;
    int client_sock = accept(listenfd, reinterpret_cast<struct sockaddr *>(&client), &client_len);
    if (client_sock < 0) {
      perror("Accept failed");
      exit(1);
    }

    printf("client_sock=%d\n", client_sock);

    // Read data until we get a full header (Seen \r\n\r\n)
    // Being simple minded and assume they all show up in a single read
    char buffer[1024];
    bool done_header_read = false;
    do {
      int count = read(client_sock, buffer, sizeof(buffer));
      if (count <= 0) {
        // Client bailed out on us
        perror("Client read failed");
        close(client_sock);
        client_sock      = -1;
        done_header_read = false;
      }
      // Not super efficient, but don't care for this test application
      for (int i = 0; i < count - 3 && !done_header_read; i++) {
        if (memcmp(buffer + i, "\r\n\r\n", 4) == 0) {
          done_header_read = true;
        }
      }
    } while (!done_header_read);

    // Send back a fixed response header
    write(client_sock, response_buffer, strlen(response_buffer));

    // Close
    close(client_sock);
    printf("Sent response\n");
  }
  printf("Finishing\n");

  exit(0);
}
