/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ts/ts.h>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <ctime>
#include <getopt.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/param.h>
#include <cinttypes>
#include <set>
#include <string>

#include "ts/ink_defs.h"

#define debug_tag(tag, fmt, ...)          \
  do {                                    \
    if (unlikely(TSIsDebugTagSet(tag))) { \
      TSDebug(tag, fmt, ##__VA_ARGS__);   \
    }                                     \
  } while (0)

#define debug(fmt, ...) debug_tag("epic", "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

#if defined(DEBUG)
#define error(fmt, ...) debug(fmt, ##__VA_ARGS__)
#else
#define error(fmt, ...) TSError("[epic]%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#endif

static TSHRTime epic_period;
static char *epic_prefix;

// XXX SSL stats (TS-2169) are going to land soon; we will need to update this list ...

// NOTE: the static list of gauge metric names does not capture dynamically constructed
// names like proxy.process.cache.volume_XX.*.active

static const std::set<std::string> epic_gauges = {
  "proxy.node.config.reconfigure_required",
  "proxy.node.config.reconfigure_time",
  "proxy.node.config.restart_required.cop",
  "proxy.node.config.restart_required.manager",
  "proxy.node.config.restart_required.proxy",
  "proxy.node.proxy_running",
  "proxy.node.restarts.manager.start_time",
  "proxy.node.restarts.proxy.cache_ready_time",
  "proxy.node.restarts.proxy.start_time",
  "proxy.node.restarts.proxy.stop_time",
  "proxy.node.version.manager.build_time",
  "proxy.process.current_server_connections",
  "proxy.process.cache.KB_read_per_sec",
  "proxy.process.cache.KB_write_per_sec",
  "proxy.process.cache.bytes_total",
  "proxy.process.cache.bytes_used",
  "proxy.process.cache.direntries.total",
  "proxy.process.cache.direntries.used",
  "proxy.process.cache.evacuate.active",
  "proxy.process.cache.lookup.active",
  "proxy.process.cache.percent_full",
  "proxy.process.cache.ram_cache.bytes_total",
  "proxy.process.cache.ram_cache.bytes_used",
  "proxy.process.cache.ram_cache.total_bytes",
  "proxy.process.cache.read.active",
  "proxy.process.cache.read_per_sec",
  "proxy.process.cache.remove.active",
  "proxy.process.cache.scan.active",
  "proxy.process.cache.update.active",
  "proxy.process.cache.write.active",
  "proxy.process.cache.write_per_sec",
  "proxy.process.dns.fail_avg_time",
  "proxy.process.dns.in_flight",
  "proxy.process.dns.lookup_avg_time",
  "proxy.process.dns.success_avg_time",
  "proxy.process.hostdb.total_entries",
  "proxy.process.http.avg_transactions_per_client_connection",
  "proxy.process.http.avg_transactions_per_parent_connection",
  "proxy.process.http.avg_transactions_per_server_connection",
  "proxy.process.http.background_fill_current_count",
  "proxy.process.http.current_active_client_connections",
  "proxy.process.http.current_cache_connections",
  "proxy.process.http.current_client_connections",
  "proxy.process.http.current_client_transactions",
  "proxy.process.http.current_parent_proxy_connections",
  "proxy.process.http.current_parent_proxy_raw_transactions",
  "proxy.process.http.current_parent_proxy_transactions",
  "proxy.process.http.current_server_connections",
  "proxy.process.http.current_server_raw_transactions",
  "proxy.process.http.current_server_transactions",
  "proxy.process.http.origin_server_speed_bytes_per_sec_100",
  "proxy.process.http.origin_server_speed_bytes_per_sec_100K",
  "proxy.process.http.origin_server_speed_bytes_per_sec_100M",
  "proxy.process.http.origin_server_speed_bytes_per_sec_10K",
  "proxy.process.http.origin_server_speed_bytes_per_sec_10M",
  "proxy.process.http.origin_server_speed_bytes_per_sec_1K",
  "proxy.process.http.origin_server_speed_bytes_per_sec_1M",
  "proxy.process.http.user_agent_speed_bytes_per_sec_100",
  "proxy.process.http.user_agent_speed_bytes_per_sec_100K",
  "proxy.process.http.user_agent_speed_bytes_per_sec_100M",
  "proxy.process.http.user_agent_speed_bytes_per_sec_10K",
  "proxy.process.http.user_agent_speed_bytes_per_sec_10M",
  "proxy.process.http.user_agent_speed_bytes_per_sec_1K",
  "proxy.process.http.user_agent_speed_bytes_per_sec_1M",
  "proxy.process.log.log_files_open",
  "proxy.process.log.log_files_space_used",
  "proxy.process.net.accepts_currently_open",
  "proxy.process.net.connections_currently_open",
  "proxy.process.socks.connections_currently_open",
  "proxy.process.update.state_machines",
  "proxy.process.version.server.build_time",
  "proxy.process.websocket.current_active_client_connections",
  "proxy.process.cache.span.failing",
  "proxy.process.cache.span.offline",
  "proxy.process.cache.span.online",
  "proxy.process.traffic_server.memory.rss",
};

struct epic_sample_context {
  time_t sample_time;
  FILE *sample_fp;
  char sample_host[TS_MAX_HOST_NAME_LEN]; /* sysconf(_SC_HOST_NAME_MAX) */
};

// Valid epic metric names contain only [A-Z] [a-z] [0-9] _ - . = >
static bool
epic_name_is_valid(const char *name)
{
  // In practice the only metrics we have that are not OK are the
  // proxy.process.cache.frags_per_doc.3+ set. Let's just check for
  // that rather than regexing everything all the time.
  return strchr(name, '+') == nullptr;
}

static void
epic_write_stats(TSRecordType /* rtype */, void *edata, int /* registered */, const char *name, TSRecordDataType dtype,
                 TSRecordData *dvalue)
{
  epic_sample_context *sample = (epic_sample_context *)edata;
  const char *etype;

  TSReleaseAssert(sample != nullptr);
  TSReleaseAssert(sample->sample_fp != nullptr);

  if (!epic_name_is_valid(name)) {
    return;
  }

  /*
   * O:varName:itime:value:node:type:step
   *
   * varName: the name of the variable being stored in 'NODE'
   * node : name space for variables, buckets of data, hostname, node, etc.
   * itime : the time in unix seconds which the datapoint is to be stored
   * value : numeric value to be stored in the ITIME time slot. Counter and
   *          Derive must be integers, not floats.
   * type: the datasource type:
   *      GAUGE: for things like temperature, or current number of processes
   *      COUNTER: for continuous incrementing numbers, inception based stats
   *              (will do counter-wrap addition at 32bit or 64bit)
   *      DERIVE: like COUNTER, except no counter-wrap detection (note: use
   *              this for Epic API data publishing)
   *      ABSOLUTE: for counters that reset upon reading
   * step: (optional) default step is 60 seconds, used here if required and
   * not sending
   */

  /* Traffic server metrics don't tell us their semantics, only their data
   * type. Mostly, metrics are counters, though a few are really gauges. This
   * sucks, but there's no workaround right now ...
   */
  etype = (epic_gauges.find(name) != epic_gauges.end()) ? "GAUGE" : "DERIVE";

  switch (dtype) {
  case TS_RECORDDATATYPE_INT:
    fprintf(sample->sample_fp, "O:%s:%lld:%" PRId64 ":%s:%s:%lld\n", name, (long long)sample->sample_time, dvalue->rec_int,
            sample->sample_host, etype, (long long)epic_period);
    break;
  case TS_RECORDDATATYPE_FLOAT:
    fprintf(sample->sample_fp, "O:%s:%lld:%f:%s:%s:%lld\n", name, (long long)sample->sample_time, dvalue->rec_float,
            sample->sample_host, etype, (long long)epic_period);
    break;
  case TS_RECORDDATATYPE_COUNTER:
    fprintf(sample->sample_fp, "O:%s:%lld:%" PRId64 ":%s:%s:%lld\n", name, (long long)sample->sample_time, dvalue->rec_counter,
            sample->sample_host, etype, (long long)epic_period);
    break;
  case TS_RECORDDATATYPE_STRING:
  case TS_RECORDDATATYPE_STAT_CONST: /* float */
  case TS_RECORDDATATYPE_STAT_FX:    /* int */
  /* fallthru */
  default:
#if defined(DEBUG)
    debug("skipping unsupported metric %s (type %d)", name, (int)dtype);
#endif
    return;
  }
}

static int
epic_flush_stats(TSCont /* contp */, TSEvent /* event */, void * /* edata */)
{
  char path[MAXPATHLEN];
  epic_sample_context sample;

  TSReleaseAssert(epic_prefix != nullptr);
  TSReleaseAssert(*epic_prefix != '\0');

  sample.sample_time = time(nullptr);
  debug("%s/trafficserver.%lld.%llu", epic_prefix, (long long)sample.sample_time, (unsigned long long)getpid());
  if (gethostname(sample.sample_host, sizeof(sample.sample_host)) == -1) {
    error("gethostname() failed: %s", strerror(errno));
    strncpy(sample.sample_host, "unknown", sizeof(sample.sample_host));
  }

  snprintf(path, sizeof(path), "%s/trafficserver.%lld.%llu", epic_prefix, (long long)sample.sample_time,
           (unsigned long long)getpid());

  // XXX track the file size and preallocate ...

  sample.sample_fp = fopen(path, "w");
  if (sample.sample_fp == nullptr) {
    error("failed to create %s: %s", path, strerror(errno));
    return 0;
  }

  TSRecordDump(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS, epic_write_stats, &sample);

  if (fclose(sample.sample_fp) == -1) {
    error("fclose() failed: %s", strerror(errno));
  }

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  static const struct option longopts[] = {
    {const_cast<char *>("directory"), required_argument, nullptr, 'd'},
    {const_cast<char *>("period"), required_argument, nullptr, 'p'},
    {nullptr, 0, nullptr, 0},
  };

  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"epic";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    error("plugin registration failed");
  }

  epic_period = 30ll; /* 30sec default */
  epic_prefix = TSstrdup("/usr/local/epic/cache/eapi");

  for (;;) {
    int opt = getopt_long(argc, (char *const *)argv, "p:d:", longopts, nullptr);

    if (opt == -1) {
      break; /* done */
    }

    switch (opt) {
    case 'd':
      TSfree(epic_prefix);
      epic_prefix = TSstrdup(optarg);
      break;
    case 'p':
      epic_period = atoi(optarg);
      break;
    default:
      error("usage: epic.so [--directory PATH] [--period SECS]");
    }
  }

  debug("initialized plugin with directory %s and period %d sec", epic_prefix, (int)epic_period);
  TSContScheduleEvery(TSContCreate(epic_flush_stats, TSMutexCreate()), epic_period * 1000ll, TS_THREAD_POOL_TASK);
}
