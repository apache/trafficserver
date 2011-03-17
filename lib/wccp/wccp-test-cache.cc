/** @file
    WCCP cache simulation for testing.

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

# include <stdio.h>
# include <unistd.h>
# include <stdarg.h>
# include <memory.h>
# include <strings.h>
# include <iostream>
# include <iomanip>

# include <getopt.h>

# include "Wccp.h"

# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>

# include <poll.h>

# include <libconfig.h++>

static char const USAGE_TEXT[] =
  "%s\n"
  "--address IP address to bind.\n"
  "--router Booststrap IP address for routers.\n"
  "--service Path to service group definitions.\n"
  "--help Print usage and exit.\n"
  ;

static bool Ready = true;

inline void Error(char const* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  Ready = false;
}

void Log(
  std::ostream& out,
  ats::Errata const& errata,
  int indent = 0
) {
  for ( ats::Errata::const_iterator spot = errata.begin(), limit = errata.end();
        spot != limit;
        ++spot
  ) {
    if (spot->m_id) {
      if (indent) out << std::setw(indent) << std::setfill(' ') << "> ";
      out << spot->m_id << " [" << spot->m_code << "]: " << spot->m_text
          << std::endl
        ;
    }
    if (spot->getErrata().size()) Log(out, spot->getErrata(), indent+2);
  }
}

void LogToStdErr(ats::Errata const& errata) {
  Log(std::cerr, errata);
}

int
main(int argc, char** argv) {
  Wccp::Cache wcp;

  // Reading stdin support.
  size_t in_size = 200;
  char* in_buff = 0;
  ssize_t in_count;

  // Set up erratum support.
  ats::Errata::registerSink(&LogToStdErr);

  // getopt return values. Selected to avoid collisions with
  // short arguments.
  static int const OPT_ADDRESS = 257; ///< Bind to IP address option.
  static int const OPT_HELP = 258; ///< Print help message.
  static int const OPT_ROUTER = 259; ///< Seeded router IP address.
  static int const OPT_SERVICE = 260; ///< Service group definition.

  static option OPTIONS[] = {
    { "address", 1, 0, OPT_ADDRESS },
    { "router", 1, 0, OPT_ROUTER },
    { "service", 1, 0, OPT_SERVICE },
    { "help", 0, 0, OPT_HELP },
    { 0, 0, 0, 0 } // required terminator.
  };

  in_addr ip_addr = { INADDR_ANY };
  in_addr router_addr = { INADDR_ANY };

  int zret; // getopt return.
  int zidx; // option index.
  bool fail = false;
  char const* FAIL_MSG = "";

  while (-1 != (zret = getopt_long_only(argc, argv, "", OPTIONS, &zidx))) {
    switch (zret) {
    case OPT_HELP:
      FAIL_MSG = "Usage:";
      fail = true;
      break;
    case '?':
      FAIL_MSG = "Invalid option specified.";
      fail = true;
      break;
    case OPT_ADDRESS:
      if (0 == inet_aton(optarg, &ip_addr)) {
        FAIL_MSG = "Invalid IP address specified for client.";
        fail = true;
      }
      break;
    case OPT_ROUTER:
      if (0 == inet_aton(optarg, &router_addr)) {
        FAIL_MSG = "Invalid IP address specified for router.";
        fail = true;
      }
      break;
    case OPT_SERVICE:
      ats::Errata status = wcp.loadServicesFromFile(optarg);
      if (!status) fail = true;
      break;
    }
  }

  if (fail) {
    printf(USAGE_TEXT, FAIL_MSG);
    return 1;
  }

  if (0 > wcp.open(ip_addr.s_addr)) {
    fprintf(stderr, "Failed to open or bind socket.\n");
    return 2;
  }

  static int const POLL_FD_COUNT = 2;
  pollfd pfa[POLL_FD_COUNT];

  // Poll on STDIN and the socket.
  pfa[0].fd = STDIN_FILENO;
  pfa[0].events = POLLIN;

  pfa[1].fd = wcp.getSocket();
  pfa[1].events = POLLIN;

  wcp.housekeeping();

  while (true) {
    time_t dt = std::min(Wccp::TIME_UNIT, wcp.waitTime());
    printf("Waiting %lu milliseconds\n", dt * 1000);
    int n = poll(pfa, POLL_FD_COUNT, dt * 1000);
    if (n < 0) { // error
      perror("General polling failure");
      return 5;
    } else if (n > 0) { // things of interest happened
      if (pfa[1].revents) {
        if (pfa[1].revents & POLLIN) {
          wcp.handleMessage();
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
      wcp.housekeeping();
    }
  }
}
