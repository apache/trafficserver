/** @file
    WCCP router simulation for testing.

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
#include <stdarg.h>
#include <memory.h>
#include <strings.h>

#include <getopt.h>

#include "Wccp.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <poll.h>

static char const USAGE_TEXT[] = "%s\n"
                                 "--address IP address to bind.\n"
                                 "--help Print usage and exit.\n";

static bool Ready = true;

inline void
Error(char const *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  Ready = false;
}

int
main(int argc, char **argv)
{
  wccp::Router wcp;

  // Reading stdin support.
  size_t in_size = 200;
  char *in_buff  = 0;
  ssize_t in_count;

  // getopt return values. Selected to avoid collisions with
  // short arguments.
  static int const OPT_ADDRESS = 257; ///< Bind to IP address option.
  static int const OPT_HELP    = 258; ///< Print help message.
  static int const OPT_MD5     = 259; ///< MD5 key.

  static option OPTIONS[] = {
    {"address", 1, 0, OPT_ADDRESS}, {"md5", 1, 0, OPT_MD5}, {"help", 0, 0, OPT_HELP}, {0, 0, 0, 0} // required terminator.
  };

  in_addr ip_addr = {INADDR_ANY};

  int zret; // getopt return.
  int zidx; // option index.
  bool fail            = false;
  char const *FAIL_MSG = "";

  while (-1 != (zret = getopt_long_only(argc, argv, "", OPTIONS, &zidx))) {
    switch (zret) {
    case OPT_HELP:
      FAIL_MSG = "Usage:";
      fail     = true;
      break;
    case '?':
      FAIL_MSG = "Invalid option specified.";
      fail     = true;
      break;
    case OPT_ADDRESS:
      if (0 == inet_aton(optarg, &ip_addr)) {
        FAIL_MSG = "Invalid IP address specified for client.";
        fail     = true;
      }
      break;
    case OPT_MD5:
      wcp.useMD5Security(optarg);
      break;
    }
  }

  if (fail) {
    printf(USAGE_TEXT, FAIL_MSG);
    return 1;
  }

  if (!Ready)
    return 4;

  if (0 > wcp.open(ip_addr.s_addr)) {
    fprintf(stderr, "Failed to open or bind socket.\n");
    return 2;
  }

  static int const POLL_FD_COUNT = 2;
  pollfd pfa[POLL_FD_COUNT];

  // Poll on STDIN and the socket.
  pfa[0].fd     = STDIN_FILENO;
  pfa[0].events = POLLIN;

  pfa[1].fd     = wcp.getSocket();
  pfa[1].events = POLLIN;

  while (true) {
    int n = poll(pfa, POLL_FD_COUNT, wccp::TIME_UNIT * 1000);
    if (n < 0) { // error
      perror("General polling failure");
      return 5;
    } else if (n > 0) { // things of interest happened
      if (pfa[1].revents) {
        if (pfa[1].revents & POLLIN) {
          wcp.handleMessage();
          // wcp.sendPendingMessages();
        } else {
          fprintf(stderr, "Socket failure.\n");
          return 6;
        }
      }
      if (pfa[0].revents) {
        if (pfa[0].revents & POLLIN) {
          in_count = getline(&in_buff, &in_size, stdin);
          fprintf(stderr, "Terminated from console.\n");
          return 0;
        }
      }
    } else { // timeout
      // wcp.sendPendingMessages();
    }
  }
}
