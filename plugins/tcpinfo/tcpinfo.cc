/** @file

  tcpinfo: A plugin to log TCP session information.

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
#include <cstdlib>
#include <memory>
#include <ts/ts.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include <climits>
#include <cstring>
#include <cerrno>
#include <sys/time.h>
#include <arpa/inet.h>

#include "ts/ink_defs.h"
#include "ts/ParseRules.h"
#include "ts/ink_std_compat.h"

#if defined(TCP_INFO) && defined(HAVE_STRUCT_TCP_INFO)
#define TCPI_PLUGIN_SUPPORTED 1
#endif

#define TCPI_HOOK_SSN_START 0x01u
#define TCPI_HOOK_TXN_START 0x02u
#define TCPI_HOOK_SEND_RESPONSE 0x04u
#define TCPI_HOOK_SSN_CLOSE 0x08u
#define TCPI_HOOK_TXN_CLOSE 0x10u

// Log format headers. These are emitted once at the start of a log file. Note that we
// carefully order the fields so the field ordering is compatible. This lets you change
// the verbosity without breaking a perser that is moderately robust.
static const char *tcpi_headers[] = {
  "timestamp event client server rtt", "timestamp event client server rtt rttvar last_sent last_recv "
                                       "snd_ssthresh rcv_ssthresh unacked sacked lost retrans fackets",
};

struct Config {
  int sample             = 1000;
  unsigned int log_level = 1;
  TSTextLogObject log    = nullptr;

  ~Config()
  {
    if (log) {
      TSTextLogObjectDestroy(log);
    }
  }
};

union const_sockaddr_ptr {
  const struct sockaddr *sa;
  const struct sockaddr_in *in;
  const struct sockaddr_in6 *in6;

  const void *
  addr() const
  {
    switch (sa->sa_family) {
    case AF_INET:
      return &(in->sin_addr);
    case AF_INET6:
      return &(in6->sin6_addr);
    default:
      return nullptr;
    }
  }
};

#if TCPI_PLUGIN_SUPPORTED

static void
log_tcp_info(Config *config, const char *event_name, TSHttpSsn ssnp)
{
  char client_str[INET6_ADDRSTRLEN];
  char server_str[INET6_ADDRSTRLEN];
  const_sockaddr_ptr client_addr;
  const_sockaddr_ptr server_addr;

  struct tcp_info info;
  socklen_t tcp_info_len = sizeof(info);
  int fd;

  TSReleaseAssert(config->log != nullptr);

  if (ssnp != nullptr && (TSHttpSsnClientFdGet(ssnp, &fd) != TS_SUCCESS || fd <= 0)) {
    TSDebug("tcpinfo", "error getting the client socket fd from ssn");
    return;
  }
  if (ssnp == nullptr) {
    TSDebug("tcpinfo", "ssn is not specified");
    return;
  }

  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &tcp_info_len) != 0) {
    TSDebug("tcpinfo", "getsockopt(%d, TCP_INFO) failed: %s", fd, strerror(errno));
    return;
  }

  client_addr.sa = TSHttpSsnClientAddrGet(ssnp);
  server_addr.sa = TSHttpSsnIncomingAddrGet(ssnp);

  if (client_addr.sa == nullptr || server_addr.sa == nullptr) {
    return;
  }

  // convert ip to string
  inet_ntop(client_addr.sa->sa_family, client_addr.addr(), client_str, sizeof(client_str));
  inet_ntop(server_addr.sa->sa_family, server_addr.addr(), server_str, sizeof(server_str));

  TSReturnCode ret;

  if (config->log_level == 2) {
#if !defined(freebsd) || defined(__GLIBC__)
    ret = TSTextLogObjectWrite(config->log, "%s %s %s %u %u %u %u %u %u %u %u %u %u %u %u %u", event_name, client_str, server_str,
                               info.tcpi_rtt, info.tcpi_rttvar, info.tcpi_last_data_sent, info.tcpi_last_data_recv,
                               info.tcpi_snd_cwnd, info.tcpi_snd_ssthresh, info.tcpi_rcv_ssthresh, info.tcpi_unacked,
                               info.tcpi_sacked, info.tcpi_lost, info.tcpi_retrans, info.tcpi_fackets, info.tcpi_total_retrans);
#else
    ret = TSTextLogObjectWrite(config->log, "%s %s %s %u %u %u %u %u %u %u %u %u %u %u %u", event_name, client_str, server_str,
                               info.tcpi_rtt, info.tcpi_rttvar, info.__tcpi_last_data_sent, info.tcpi_last_data_recv,
                               info.tcpi_snd_cwnd, info.tcpi_snd_ssthresh, info.__tcpi_rcv_ssthresh, info.__tcpi_unacked,
                               info.__tcpi_sacked, info.__tcpi_lost, info.__tcpi_retrans, info.__tcpi_fackets);
#endif
  } else {
    ret = TSTextLogObjectWrite(config->log, "%s %s %s %u", event_name, client_str, server_str, info.tcpi_rtt);
  }

  if (ret != TS_SUCCESS) {
    // ToDo: This could be due to a failure, or logs full. Should we consider
    // closing / reopening the log? If so, how often do we do that ?
  }
}

#else /* TCPI_PLUGIN_SUPPORTED */

static void
log_tcp_info(Config * /* config */, const char * /* event_name */, TSHttpSsn /* ssnp */)
{
  return; // TCP metrics not supported.
}

#endif /* TCPI_PLUGIN_SUPPORTED */

static int
tcp_info_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = nullptr;
  TSHttpTxn txnp = nullptr;
  int random     = 0;
  Config *config = (Config *)TSContDataGet(contp);

  const char *event_name;
  switch (event) {
  case TS_EVENT_HTTP_SSN_START:
    ssnp       = (TSHttpSsn)edata;
    event_name = "ssn_start";
    break;
  case TS_EVENT_HTTP_TXN_START:
    txnp       = (TSHttpTxn)edata;
    ssnp       = TSHttpTxnSsnGet(txnp);
    event_name = "txn_start";
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    txnp       = (TSHttpTxn)edata;
    ssnp       = TSHttpTxnSsnGet(txnp);
    event_name = "txn_close";
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    txnp       = (TSHttpTxn)edata;
    ssnp       = TSHttpTxnSsnGet(txnp);
    event_name = "send_resp_hdr";
    break;
  case TS_EVENT_HTTP_SSN_CLOSE:
    ssnp       = (TSHttpSsn)edata;
    event_name = "ssn_close";
    break;
  default:
    return 0;
  }

  TSDebug("tcpinfo", "logging hook called for %s (%s) with log object %p", TSHttpEventNameLookup(event), event_name, config->log);

  if (config->log == nullptr) {
    goto done;
  }

  // Don't try to sample internal requests. TCP metrics for loopback are not interesting.
  if (TSHttpSsnIsInternal(ssnp)) {
    goto done;
  }

  // no need to run rand if we are always going log (100%)
  if (config->sample < 1000) {
    // coverity[dont_call]
    random = rand() % 1000;
    TSDebug("tcpinfo", "random: %d, config->sample: %d", random, config->sample);
  }

  if (random < config->sample) {
    TSDebug("tcpinfo", "sampling TCP metrics for %s event", event_name);
    log_tcp_info(config, event_name, ssnp);
  }

done:
  if (txnp != nullptr) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } else if (ssnp != nullptr) {
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  }

  return TS_EVENT_NONE;
}

static bool
parse_unsigned(const char *str, unsigned long &lval)
{
  char *end = nullptr;

  if (*str == '\0') {
    return false;
  }

  lval = strtoul(str, &end, 0 /* base */);
  if (end == str) {
    // No valid characters.
    return false;
  }

  if (end && *end != '\0') {
    // Not all charaters consumed.
    return false;
  }

  return true;
}

// Parse a comma-separated list of hook names into a hook bitmask.
static unsigned
parse_hook_list(const char *hook_list)
{
  unsigned mask = 0;
  char *tok;
  char *str;
  char *last;

  const struct hookmask {
    const char *name;
    unsigned mask;
  } hooks[] = {{"ssn_start", TCPI_HOOK_SSN_START}, {"txn_start", TCPI_HOOK_TXN_START}, {"send_resp_hdr", TCPI_HOOK_SEND_RESPONSE},
               {"ssn_close", TCPI_HOOK_SSN_CLOSE}, {"txn_close", TCPI_HOOK_TXN_CLOSE}, {nullptr, 0u}};

  str = TSstrdup(hook_list);

  for (tok = strtok_r(str, ",", &last); tok; tok = strtok_r(nullptr, ",", &last)) {
    bool match = false;

    for (const struct hookmask *m = hooks; m->name != nullptr; ++m) {
      if (strcmp(m->name, tok) == 0) {
        mask |= m->mask;
        match = true;
        break;
      }
    }

    if (!match) {
      TSError("[tcpinfo] invalid hook name '%s'", tok);
    }
  }

  TSfree(str);
  return mask;
}

void
TSPluginInit(int argc, const char *argv[])
{
  static const char usage[] = "tcpinfo.so [--log-file=PATH] [--log-level=LEVEL] [--hooks=LIST] [--sample-rate=COUNT] "
                              "[--rolling-enabled=VALUE] [--rolling-offset-hr=HOUR] [--rolling-interval-sec=SECONDS] "
                              "[--rolling-size=MB]";
  static const struct option longopts[] = {
    {const_cast<char *>("sample-rate"), required_argument, nullptr, 'r'},
    {const_cast<char *>("log-file"), required_argument, nullptr, 'f'},
    {const_cast<char *>("log-level"), required_argument, nullptr, 'l'},
    {const_cast<char *>("hooks"), required_argument, nullptr, 'h'},
    {const_cast<char *>("rolling-enabled"), required_argument, nullptr, 'e'},
    {const_cast<char *>("rolling-offset-hr"), required_argument, nullptr, 'H'},
    {const_cast<char *>("rolling-interval-sec"), required_argument, nullptr, 'S'},
    {const_cast<char *>("rolling-size"), required_argument, nullptr, 'M'},
    {nullptr, 0, nullptr, 0},
  };

  TSPluginRegistrationInfo info;
  auto config          = std::make_unique<Config>();
  const char *filename = "tcpinfo";
  TSCont cont;
  unsigned int hooks                = 0;
  unsigned int rolling_enabled      = 1;
  unsigned int rolling_interval_sec = 86400;
  unsigned int rolling_offset_hr    = 0;
  unsigned int rolling_size         = 1024;
  unsigned int i                    = 0;
  char *endptr;

  info.plugin_name   = (char *)"tcpinfo";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[tcpinfo] plugin registration failed");
  }

  for (;;) {
    unsigned long lval;

    switch (getopt_long(argc, (char *const *)argv, "r:f:l:h:e:H:S:M:", longopts, nullptr)) {
    case 'r':
      if (parse_unsigned(optarg, lval)) {
        config->sample = atoi(optarg);
      } else {
        TSError("[tcpinfo] invalid sample rate '%s'", optarg);
      }
      break;
    case 'f':
      filename = optarg;
      break;
    case 'l':
      if (parse_unsigned(optarg, lval) && (lval <= countof(tcpi_headers))) {
        config->log_level = lval;
      } else {
        TSError("[tcpinfo] invalid log level '%s'", optarg);
      }
      break;
    case 'h':
      hooks = parse_hook_list(optarg);
      break;
    case 'e':
      i = strtoul(optarg, &endptr, 10);
      if (*endptr != '\0' || i > 3) {
        TSError("[tcpinfo] invalid rolling-enabled argument, '%s', using default of %d", optarg, rolling_enabled);
      } else {
        rolling_enabled = i;
      }
      break;
    case 'H':
      i = strtoul(optarg, &endptr, 10);
      if (*endptr != '\0' || i > 23) {
        TSError("[tcpinfo] invalid rolling-offset-hr argument, '%s', using default of %d", optarg, rolling_offset_hr);
      } else {
        rolling_offset_hr = i;
      }
      break;
    case 'S':
      i = strtoul(optarg, &endptr, 10);
      if (*endptr != '\0' || i < 60 || i > 86400) {
        TSError("[tcpinfo] invalid rolling-interval-sec argument, '%s', using default of %d", optarg, rolling_interval_sec);
      } else {
        rolling_interval_sec = i;
      }
      break;
    case 'M':
      i = ink_atoui(optarg);
      if (i < 10) {
        TSError("[tcpinfo] invalid rolling-size argument, '%s', using default of %d", optarg, rolling_size);
      } else {
        rolling_size = i;
      }
      break;
    case -1:
      goto init;
    default:
      TSError("[tcpinfo] usage: %s", usage);
    }
  }

init:

#if !TCPI_PLUGIN_SUPPORTED
  TSError("[tcpinfo] TCP metrics are not supported on this platform");
#endif

  TSDebug("tcpinfo", "sample: %d", config->sample);
  TSDebug("tcpinfo", "log filename: %s", filename);
  TSDebug("tcpinfo", "log_level: %u", config->log_level);
  TSDebug("tcpinfo", "hook mask: 0x%x", hooks);

  if (TSTextLogObjectCreate(filename, TS_LOG_MODE_ADD_TIMESTAMP, &config->log) != TS_SUCCESS) {
    TSError("[tcpinfo] failed to create log file '%s'", filename);
    return;
  }
  if (TSTextLogObjectRollingEnabledSet(config->log, rolling_enabled) != TS_SUCCESS) {
    TSError("[tcpinfo] failed to enable log file rolling to: '%d'", rolling_enabled);
    return;
  } else {
    TSTextLogObjectRollingIntervalSecSet(config->log, rolling_interval_sec);
    TSTextLogObjectRollingOffsetHrSet(config->log, rolling_offset_hr);
    TSTextLogObjectRollingSizeMbSet(config->log, rolling_size);
  }

  TSTextLogObjectHeaderSet(config->log, tcpi_headers[config->log_level - 1]);

  cont = TSContCreate(tcp_info_hook, nullptr);
  TSContDataSet(cont, config.release());

  if (hooks & TCPI_HOOK_SSN_START) {
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, cont);
    TSDebug("tcpinfo", "added hook to the start of the TCP connection");
  }

  if (hooks & TCPI_HOOK_TXN_START) {
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
    TSDebug("tcpinfo", "added hook to the start of the transaction");
  }

  if (hooks & TCPI_HOOK_SEND_RESPONSE) {
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
    TSDebug("tcpinfo", "added hook to the sending of the headers");
  }

  if (hooks & TCPI_HOOK_SSN_CLOSE) {
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, cont);
    TSDebug("tcpinfo", "added hook to the close of the TCP connection");
  }

  if (hooks & TCPI_HOOK_TXN_CLOSE) {
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, cont);
    TSDebug("tcpinfo", "added hook to the close of the transaction");
  }
}
