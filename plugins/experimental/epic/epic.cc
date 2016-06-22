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
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <inttypes.h>
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

#define GAUGE_METRIC_NAMES                                                                                                         \
  "proxy.node.bandwidth_hit_ratio_avg_10s", "proxy.node.bandwidth_hit_ratio_avg_10s",                                              \
    "proxy.node.bandwidth_hit_ratio_avg_10s_int_pct", "proxy.node.bandwidth_hit_ratio_int_pct", "proxy.node.cache.bytes_free",     \
    "proxy.node.cache.bytes_free_mb", "proxy.node.cache.bytes_total", "proxy.node.cache.bytes_total_mb",                           \
    "proxy.node.cache.bytes_used", "proxy.node.cache.bytes_used_mb", "proxy.node.cache.percent_free",                              \
    "proxy.node.cache.percent_free_int_pct", "proxy.node.cache_hit_mem_ratio", "proxy.node.cache_hit_mem_ratio_avg_10s",           \
    "proxy.node.cache_hit_mem_ratio_avg_10s_int_pct", "proxy.node.cache_hit_mem_ratio_int_pct", "proxy.node.cache_hit_ratio",      \
    "proxy.node.cache_hit_ratio_avg_10s", "proxy.node.cache_hit_ratio_avg_10s_int_pct", "proxy.node.cache_hit_ratio_int_pct",      \
    "proxy.node.cache_total_hits_avg_10s", "proxy.node.cache_total_hits_mem_avg_10s", "proxy.node.cache_total_misses_avg_10s",     \
    "proxy.node.client_throughput_out", "proxy.node.client_throughput_out_kbit", "proxy.node.cluster.nodes",                       \
    "proxy.node.config.reconfigure_required", "proxy.node.config.reconfigure_time", "proxy.node.config.restart_required.cop",      \
    "proxy.node.config.restart_required.manager", "proxy.node.config.restart_required.proxy",                                      \
    "proxy.node.current_cache_connections", "proxy.node.current_client_connections", "proxy.node.current_server_connections",      \
    "proxy.node.dns.lookup_avg_time_ms", "proxy.node.dns.lookups_per_second", "proxy.node.hostdb.hit_ratio",                       \
    "proxy.node.hostdb.hit_ratio_avg_10s", "proxy.node.hostdb.hit_ratio_int_pct", "proxy.node.hostdb.total_hits_avg_10s",          \
    "proxy.node.hostdb.total_lookups_avg_10s", "proxy.node.http.cache_current_connections_count",                                  \
    "proxy.node.http.cache_hit_fresh_avg_10s", "proxy.node.http.cache_hit_ims_avg_10s",                                            \
    "proxy.node.http.cache_hit_mem_fresh_avg_10s", "proxy.node.http.cache_hit_revalidated_avg_10s",                                \
    "proxy.node.http.cache_hit_stale_served_avg_10s", "proxy.node.http.cache_miss_changed_avg_10s",                                \
    "proxy.node.http.cache_miss_client_no_cache_avg_10s", "proxy.node.http.cache_miss_cold_avg_10s",                               \
    "proxy.node.http.cache_miss_ims_avg_10s", "proxy.node.http.cache_miss_not_cacheable_avg_10s",                                  \
    "proxy.node.http.cache_read_error_avg_10s", "proxy.node.http.current_parent_proxy_connections",                                \
    "proxy.node.http.origin_server_current_connections_count", "proxy.node.http.transaction_counts_avg_10s.errors.aborts",         \
    "proxy.node.http.transaction_counts_avg_10s.errors.connect_failed",                                                            \
    "proxy.node.http.transaction_counts_avg_10s.errors.early_hangups",                                                             \
    "proxy.node.http.transaction_counts_avg_10s.errors.empty_hangups", "proxy.node.http.transaction_counts_avg_10s.errors.other",  \
    "proxy.node.http.transaction_counts_avg_10s.errors.possible_aborts",                                                           \
    "proxy.node.http.transaction_counts_avg_10s.errors.pre_accept_hangups",                                                        \
    "proxy.node.http.transaction_counts_avg_10s.hit_fresh", "proxy.node.http.transaction_counts_avg_10s.hit_revalidated",          \
    "proxy.node.http.transaction_counts_avg_10s.miss_changed", "proxy.node.http.transaction_counts_avg_10s.miss_client_no_cache",  \
    "proxy.node.http.transaction_counts_avg_10s.miss_cold", "proxy.node.http.transaction_counts_avg_10s.miss_not_cacheable",       \
    "proxy.node.http.transaction_counts_avg_10s.other.unclassified", "proxy.node.http.transaction_frac_avg_10s.errors.aborts",     \
    "proxy.node.http.transaction_frac_avg_10s.errors.aborts_int_pct",                                                              \
    "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed",                                                              \
    "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed_int_pct",                                                      \
    "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups",                                                               \
    "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups_int_pct",                                                       \
    "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups",                                                               \
    "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups_int_pct",                                                       \
    "proxy.node.http.transaction_frac_avg_10s.errors.other", "proxy.node.http.transaction_frac_avg_10s.errors.other_int_pct",      \
    "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts",                                                             \
    "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts_int_pct",                                                     \
    "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups",                                                          \
    "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups_int_pct",                                                  \
    "proxy.node.http.transaction_frac_avg_10s.hit_fresh", "proxy.node.http.transaction_frac_avg_10s.hit_fresh_int_pct",            \
    "proxy.node.http.transaction_frac_avg_10s.hit_revalidated",                                                                    \
    "proxy.node.http.transaction_frac_avg_10s.hit_revalidated_int_pct", "proxy.node.http.transaction_frac_avg_10s.miss_changed",   \
    "proxy.node.http.transaction_frac_avg_10s.miss_changed_int_pct",                                                               \
    "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache",                                                               \
    "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache_int_pct", "proxy.node.http.transaction_frac_avg_10s.miss_cold", \
    "proxy.node.http.transaction_frac_avg_10s.miss_cold_int_pct", "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable",   \
    "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable_int_pct",                                                         \
    "proxy.node.http.transaction_frac_avg_10s.other.unclassified",                                                                 \
    "proxy.node.http.transaction_frac_avg_10s.other.unclassified_int_pct",                                                         \
    "proxy.node.http.transaction_msec_avg_10s.errors.aborts", "proxy.node.http.transaction_msec_avg_10s.errors.connect_failed",    \
    "proxy.node.http.transaction_msec_avg_10s.errors.early_hangups",                                                               \
    "proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups", "proxy.node.http.transaction_msec_avg_10s.errors.other",      \
    "proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts",                                                             \
    "proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups", "proxy.node.http.transaction_msec_avg_10s.hit_fresh",    \
    "proxy.node.http.transaction_msec_avg_10s.hit_revalidated", "proxy.node.http.transaction_msec_avg_10s.miss_changed",           \
    "proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache", "proxy.node.http.transaction_msec_avg_10s.miss_cold",         \
    "proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable", "proxy.node.http.transaction_msec_avg_10s.other.unclassified",  \
    "proxy.node.http.user_agent_current_connections_count", "proxy.node.http.user_agent_xacts_per_second",                         \
    "proxy.node.log.bytes_received_from_network_avg_10s", "proxy.node.log.bytes_sent_to_network_avg_10s",                          \
    "proxy.node.num_processes", "proxy.node.origin_server_total_bytes_avg_10s", "proxy.node.proxy_running",                        \
    "proxy.node.restarts.manager.start_time", "proxy.node.restarts.proxy.cache_ready_time",                                        \
    "proxy.node.restarts.proxy.start_time", "proxy.node.restarts.proxy.stop_time", "proxy.node.user_agent_total_bytes_avg_10s",    \
    "proxy.node.user_agent_xacts_per_second", "proxy.node.version.manager.build_time", "proxy.process.cache.KB_read_per_sec",      \
    "proxy.process.cache.KB_write_per_sec", "proxy.process.cache.bytes_total", "proxy.process.cache.bytes_used",                   \
    "proxy.process.cache.direntries.total", "proxy.process.cache.direntries.used", "proxy.process.cache.evacuate.active",          \
    "proxy.process.cache.lookup.active", "proxy.process.cache.percent_full", "proxy.process.cache.ram_cache.bytes_total",          \
    "proxy.process.cache.ram_cache.bytes_used", "proxy.process.cache.ram_cache.total_bytes", "proxy.process.cache.read.active",    \
    "proxy.process.cache.read_per_sec", "proxy.process.cache.remove.active", "proxy.process.cache.scan.active",                    \
    "proxy.process.cache.update.active", "proxy.process.cache.write.active", "proxy.process.cache.write_per_sec",                  \
    "proxy.process.cluster.cache_callback_time", "proxy.process.cluster.cache_outstanding",                                        \
    "proxy.process.cluster.cluster_ping_time", "proxy.process.cluster.connections_avg_time",                                       \
    "proxy.process.cluster.connections_open", "proxy.process.cluster.control_messages_avg_receive_time",                           \
    "proxy.process.cluster.control_messages_avg_send_time", "proxy.process.cluster.lkrmt_cache_callback_time",                     \
    "proxy.process.cluster.local_connection_time", "proxy.process.cluster.open_delay_time",                                        \
    "proxy.process.cluster.rdmsg_assemble_time", "proxy.process.cluster.remote_connection_time",                                   \
    "proxy.process.cluster.remote_op_reply_timeouts", "proxy.process.cluster.remote_op_timeouts",                                  \
    "proxy.process.cluster.rmt_cache_callback_time", "proxy.process.dns.fail_avg_time", "proxy.process.dns.in_flight",             \
    "proxy.process.dns.lookup_avg_time", "proxy.process.dns.success_avg_time", "proxy.process.hostdb.total_entries",               \
    "proxy.process.http.avg_transactions_per_client_connection", "proxy.process.http.avg_transactions_per_parent_connection",      \
    "proxy.process.http.avg_transactions_per_server_connection", "proxy.process.http.background_fill_current_count",               \
    "proxy.process.http.current_active_client_connections", "proxy.process.http.current_cache_connections",                        \
    "proxy.process.http.current_client_connections", "proxy.process.http.current_client_transactions",                             \
    "proxy.process.http.current_icp_raw_transactions", "proxy.process.http.current_icp_transactions",                              \
    "proxy.process.http.current_parent_proxy_connections", "proxy.process.http.current_parent_proxy_raw_transactions",             \
    "proxy.process.http.current_parent_proxy_transactions", "proxy.process.http.current_server_connections",                       \
    "proxy.process.http.current_server_raw_transactions", "proxy.process.http.current_server_transactions",                        \
    "proxy.process.http.origin_server_speed_bytes_per_sec_100", "proxy.process.http.origin_server_speed_bytes_per_sec_100K",       \
    "proxy.process.http.origin_server_speed_bytes_per_sec_100M", "proxy.process.http.origin_server_speed_bytes_per_sec_10K",       \
    "proxy.process.http.origin_server_speed_bytes_per_sec_10M", "proxy.process.http.origin_server_speed_bytes_per_sec_1K",         \
    "proxy.process.http.origin_server_speed_bytes_per_sec_1M", "proxy.process.http.user_agent_speed_bytes_per_sec_100",            \
    "proxy.process.http.user_agent_speed_bytes_per_sec_100K", "proxy.process.http.user_agent_speed_bytes_per_sec_100M",            \
    "proxy.process.http.user_agent_speed_bytes_per_sec_10K", "proxy.process.http.user_agent_speed_bytes_per_sec_10M",              \
    "proxy.process.http.user_agent_speed_bytes_per_sec_1K", "proxy.process.http.user_agent_speed_bytes_per_sec_1M",                \
    "proxy.process.log.log_files_open", "proxy.process.log.log_files_space_used", "proxy.process.net.accepts_currently_open",      \
    "proxy.process.net.connections_currently_open", "proxy.process.socks.connections_currently_open",                              \
    "proxy.process.update.state_machines", "proxy.process.version.server.build_time",                                              \
    "proxy.process.websocket.current_active_client_connections"

// XXX SSL stats (TS-2169) are going to land soon; we will need to update this list ...

// NOTE: the static list of gauge metric names does not capture dynamically constructed
// names like proxy.process.cache.volume_XX.*.active

#if __cplusplus >= 201102L

static const std::set<std::string> epic_gauges = {GAUGE_METRIC_NAMES};

#else

static std::set<std::string>
init_gauges()
{
  static const char *gauges[] = {GAUGE_METRIC_NAMES};

  std::set<std::string> s;

  for (unsigned i = 0; i < countof(gauges); ++i) {
    s.insert(gauges[i]);
  }

  return s;
}

static const std::set<std::string> epic_gauges(init_gauges());

#endif

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
  return strchr(name, '+') == NULL;
}

static void
epic_write_stats(TSRecordType /* rtype */, void *edata, int /* registered */, const char *name, TSRecordDataType dtype,
                 TSRecordData *dvalue)
{
  epic_sample_context *sample = (epic_sample_context *)edata;
  const char *etype;

  TSReleaseAssert(sample != NULL);
  TSReleaseAssert(sample->sample_fp != NULL);

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

  TSReleaseAssert(epic_prefix != NULL);
  TSReleaseAssert(*epic_prefix != '\0');

  sample.sample_time = time(NULL);
  debug("%s/trafficserver.%lld.%llu", epic_prefix, (long long)sample.sample_time, (unsigned long long)getpid());
  if (gethostname(sample.sample_host, sizeof(sample.sample_host)) == -1) {
    error("gethostname() failed: %s", strerror(errno));
    strncpy(sample.sample_host, "unknown", sizeof(sample.sample_host));
  }

  snprintf(path, sizeof(path), "%s/trafficserver.%lld.%llu", epic_prefix, (long long)sample.sample_time,
           (unsigned long long)getpid());

  // XXX track the file size and preallocate ...

  sample.sample_fp = fopen(path, "w");
  if (sample.sample_fp == NULL) {
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
  static const struct option longopts[] = {{const_cast<char *>("directory"), required_argument, NULL, 'd'},
                                           {const_cast<char *>("period"), required_argument, NULL, 'p'},
                                           {NULL, 0, NULL, 0}};

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
    int opt = getopt_long(argc, (char *const *)argv, "p:d:", longopts, NULL);

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
  TSContScheduleEvery(TSContCreate(epic_flush_stats, NULL), epic_period * 1000ll, TS_THREAD_POOL_TASK);
}
