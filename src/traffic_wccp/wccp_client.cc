/** @file
    WCCP cache client

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

#include <cstdio>
#include <unistd.h>
#include <cstdarg>
#include <memory.h>
#include <strings.h>
#include <iostream>
#include <iomanip>

#include <getopt.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <poll.h>

#include "tscore/ink_memory.h"
#include "wccp/Wccp.h"
#include "../wccp/WccpUtil.h"
#include "tscore/ink_lockfile.h"
#include "tscore/Errata.h"

#define WCCP_LOCK "wccp.pid"

bool do_debug  = false;
bool do_daemon = false;

static const char USAGE_TEXT[] = "%s\n"
                                 "--address IP address to bind.\n"
                                 "--router Bootstrap IP address for routers.\n"
                                 "--service Path to service group definitions.\n"
                                 "--debug Print debugging information.\n"
                                 "--daemon Run as daemon.\n"
                                 "--help Print usage and exit.\n";

static void
PrintErrata(ts::Errata const &err)
{
  size_t n;
  static size_t const SIZE = 4096;
  char buff[SIZE];
  if (err.size()) {
    ts::Errata::Code code = err.top().getCode();
    if (do_debug || code >= wccp::LVL_WARN) {
      n = err.write(buff, SIZE, 1, 0, 2, "> ");
      // strip trailing newlines.
      while (n && (buff[n - 1] == '\n' || buff[n - 1] == '\r')) {
        buff[--n] = 0;
      }
      printf("%s\n", buff);
    }
  }
}

static void
Init_Errata_Logging()
{
  ts::Errata::registerSink(&PrintErrata);
}

static void
check_lockfile()
{
  char lockfile[256];
  pid_t holding_pid;
  int err;

  strcpy(lockfile, "/var/run/");
  strcat(lockfile, WCCP_LOCK);

  Lockfile server_lockfile(lockfile);
  err = server_lockfile.Get(&holding_pid);

  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "WARNING: Can't acquire lockfile '%s'", (const char *)lockfile);

    if ((err == 0) && (holding_pid != -1)) {
      fprintf(stderr, " (Lock file held by process ID %l" PRIu32 ")\n", static_cast<long>(holding_pid));
    } else if ((err == 0) && (holding_pid == -1)) {
      fprintf(stderr, " (Lock file exists, but can't read process ID)\n");
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    ::exit(1);
  }
}

int
main(int argc, char **argv)
{
  wccp::Cache wcp;

  // getopt return values. Selected to avoid collisions with
  // short arguments.
  static int const OPT_ADDRESS = 257; ///< Bind to IP address option.
  static int const OPT_HELP    = 258; ///< Print help message.
  static int const OPT_ROUTER  = 259; ///< Seeded router IP address.
  static int const OPT_SERVICE = 260; ///< Service group definition.
  static int const OPT_DEBUG   = 261; ///< Enable debug printing
  static int const OPT_DAEMON  = 262; ///< Disconnect and run as daemon

  static option OPTIONS[] = {
    {"address", 1, nullptr, OPT_ADDRESS},
    {"router", 1, nullptr, OPT_ROUTER},
    {"service", 1, nullptr, OPT_SERVICE},
    {"debug", 0, nullptr, OPT_DEBUG},
    {"daemon", 0, nullptr, OPT_DAEMON},
    {"help", 0, nullptr, OPT_HELP},
    {nullptr, 0, nullptr, 0} // required terminator.
  };

  in_addr ip_addr     = {INADDR_ANY};
  in_addr router_addr = {INADDR_ANY};

  int zret; // getopt return.
  int zidx; // option index.
  bool fail            = false;
  const char *FAIL_MSG = "";

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
    case OPT_ROUTER:
      if (0 == inet_aton(optarg, &router_addr)) {
        FAIL_MSG = "Invalid IP address specified for router.";
        fail     = true;
      }
      break;
    case OPT_SERVICE: {
      ts::Errata status = wcp.loadServicesFromFile(optarg);
      if (!status) {
        fail = true;
      }
      break;
    }
    case OPT_DEBUG:
      do_debug = true;
      break;
    case OPT_DAEMON:
      do_daemon = true;
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

  if (do_daemon) {
    pid_t pid = fork();
    if (pid > 0) {
      // Successful, the parent should go away
      ::exit(0);
    }
  }

  check_lockfile();

  // Set up erratum support.
  Init_Errata_Logging();

  static int const POLL_FD_COUNT = 1;
  pollfd pfa[POLL_FD_COUNT];

  // Poll on the socket.
  pfa[0].fd     = wcp.getSocket();
  pfa[0].events = POLLIN;

  wcp.housekeeping();

  while (true) {
    int n = poll(pfa, POLL_FD_COUNT, 1000);
    if (n < 0) { // error
      perror("General polling failure");
      return 5;
    } else if (n > 0) { // things of interest happened
      if (pfa[0].revents) {
        if (pfa[0].revents & POLLIN) {
          wcp.handleMessage();
        } else {
          fprintf(stderr, "Socket failure.\n");
          return 6;
        }
      }
    } else { // timeout
      wcp.housekeeping();
    }
  }
}
