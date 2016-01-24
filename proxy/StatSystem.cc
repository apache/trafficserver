/** @file

  A brief file description

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

/****************************************************************************

  StatSystem.cc --
  Created On          : Fri Apr 3 19:41:39 1998
 ****************************************************************************/
#include "Main.h"
#include "StatSystem.h"
#include "P_EventSystem.h"
#include "Error.h"
#include "ProcessManager.h"
#include "ProxyConfig.h"
#include "StatPages.h"
#include "HTTP.h"
#include "ts/I_Layout.h"

// defines

#define SNAP_USAGE_PERIOD HRTIME_SECONDS(2)


// variables

#ifdef DEBUG
ink_mutex http_time_lock;
time_t last_http_local_time;
#endif
ink_stat_lock_t global_http_trans_stat_lock;
ink_unprot_global_stat_t global_http_trans_stats[MAX_HTTP_TRANS_STATS];
#ifndef USE_LOCKS_FOR_DYN_STATS
inkcoreapi ink_unprot_global_stat_t global_dyn_stats[MAX_DYN_STATS - DYN_STAT_START];
#else
inkcoreapi ink_prot_global_stat_t global_dyn_stats[MAX_DYN_STATS - DYN_STAT_START];
#endif

Ptr<ProxyMutex> rusage_snap_mutex;
struct rusage rusage_snap;
struct rusage rusage_snap_old;
ink_hrtime rusage_snap_time;
ink_hrtime rusage_snap_time_old;
int snap_stats_every = 60;

ink_hrtime http_handler_times[MAX_HTTP_HANDLER_EVENTS];
int http_handler_counts[MAX_HTTP_HANDLER_EVENTS];


char snap_filename[PATH_NAME_MAX] = DEFAULT_SNAP_FILENAME;

#define DEFAULT_PERSISTENT

#ifndef DEFAULT_PERSISTENT
static int persistent_stats[] = {http_incoming_requests_stat};
#else
static int non_persistent_stats[] = {
  ////////////////////////////
  // Start of Cluster stats
  ////////////////////////////
  cluster_connections_open_stat, cluster_connections_openned_stat, cluster_con_total_time_stat, cluster_ctrl_msgs_sent_stat,
  cluster_slow_ctrl_msgs_sent_stat, cluster_ctrl_msgs_recvd_stat, cluster_slow_ctrl_msgs_recvd_stat,
  cluster_ctrl_msgs_send_time_stat, cluster_ctrl_msgs_recv_time_stat, cluster_read_bytes_stat, cluster_write_bytes_stat,
  cluster_op_delayed_for_lock_stat, cluster_connections_locked_stat, cluster_connections_bumped_stat, cluster_nodes_stat,
  cluster_net_backup_stat, cluster_machines_allocated_stat, cluster_machines_freed_stat, cluster_configuration_changes_stat,
  cluster_delayed_reads_stat, cluster_byte_bank_used_stat, cluster_alloc_data_news_stat, cluster_write_bb_mallocs_stat,
  cluster_partial_reads_stat, cluster_partial_writes_stat, cluster_cache_outstanding_stat, cluster_remote_op_timeouts_stat,
  cluster_remote_op_reply_timeouts_stat, cluster_chan_inuse_stat, cluster_open_delays_stat, cluster_open_delay_time_stat,
  cluster_cache_callbacks_stat, cluster_cache_callback_time_stat, cluster_cache_rmt_callbacks_stat,
  cluster_cache_rmt_callback_time_stat, cluster_cache_lkrmt_callbacks_stat, cluster_cache_lkrmt_callback_time_stat,
  cluster_thread_steal_expires_stat, cluster_local_connections_closed_stat, cluster_local_connection_time_stat,
  cluster_remote_connections_closed_stat, cluster_remote_connection_time_stat, cluster_rdmsg_assemble_time_stat,
  cluster_ping_time_stat, cluster_setdata_no_clustervc_stat, cluster_setdata_no_tunnel_stat, cluster_setdata_no_cachevc_stat,
  cluster_setdata_no_cluster_stat, cluster_vc_write_stall_stat, cluster_no_remote_space_stat, cluster_level1_bank_stat,
  cluster_multilevel_bank_stat, cluster_vc_cache_insert_lock_misses_stat, cluster_vc_cache_inserts_stat,
  cluster_vc_cache_lookup_lock_misses_stat, cluster_vc_cache_lookup_hits_stat, cluster_vc_cache_lookup_misses_stat,
  cluster_vc_cache_scans_stat, cluster_vc_cache_scan_lock_misses_stat, cluster_vc_cache_purges_stat, cluster_write_lock_misses_stat,
  /////////////////////////////////////
  // Start of Scheduled Update stats
  /////////////////////////////////////
  // DNS
  // dns_success_time_stat
};
#endif

#define _HEADER DynamicStatsString_t DynamicStatsStrings[] = {
#define _FOOTER \
  }             \
  ;
#define _D(_x) \
  {            \
    _x, #_x    \
  }            \
  ,

#include "DynamicStats.h"
#undef _HEADER
#undef _FOOTER
#undef _D


// functions

static int
persistent_stat(int i)
{
#ifndef DEFAULT_PERSISTENT
  for (unsigned j = 0; j < countof(persistent_stats); j++)
    if (persistent_stats[j] == i)
      return 1;
  return 0;
#else
  for (unsigned j = 0; j < countof(non_persistent_stats); j++)
    if (non_persistent_stats[j] == i)
      return 0;
  return 1;
#endif
}

static int
open_stats_snap()
{
  int fd = socketManager.open(snap_filename, O_CREAT | O_RDWR);
  if (fd < 0) {
    Warning("unable to open %s: %s", snap_filename, strerror(-fd));
    return -1;
  }
  return fd;
}

static void
clear_stats()
{
  int i = 0;

  int stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS - 1;
  for (i = 0; i < stats_size; i++) {
    if (persistent_stat(i + NO_HTTP_TRANS_STATS)) {
      global_http_trans_stats[i].sum = 0;
      global_http_trans_stats[i].count = 0;
    }
  }
  stats_size = MAX_DYN_STATS - NO_DYN_STATS - 1;
  for (i = 0; i < stats_size; i++) {
    if (persistent_stat(i + NO_DYN_STATS)) {
      global_dyn_stats[i].sum = 0;
      global_dyn_stats[i].count = 0;
    }
  }

  socketManager.unlink(snap_filename);
  Debug("stats", "clear_stats: clearing statistics");
}

static void
read_stats_snap()
{
  unsigned int version;
  unsigned int version_read;
  int count;
  int fd = -1;
  int i = 0;
  int stats_size = -1;

  version = STATS_MAJOR_VERSION;

  if ((fd = open_stats_snap()) < 0)
    goto Lmissmatch;

  // read and verify snap
  if (socketManager.read(fd, (char *)&version_read, sizeof(version_read)) != sizeof(version_read))
    goto Lmissmatch;
  if (version != version_read)
    goto Lmissmatch;
  stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS + MAX_DYN_STATS - NO_DYN_STATS;
  if (socketManager.read(fd, (char *)&count, sizeof(count)) != sizeof(count))
    goto Lmissmatch;
  if (count != stats_size)
    goto Lmissmatch;

  stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *)&global_http_trans_stats[i].sum, sizeof(global_http_trans_stats[i].sum)) !=
        sizeof(global_http_trans_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *)&global_http_trans_stats[i].count, sizeof(global_http_trans_stats[i].count)) !=
        sizeof(global_http_trans_stats[i].count))
      goto Lmissmatch;
  }
  stats_size = MAX_DYN_STATS - NO_DYN_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *)&global_dyn_stats[i].sum, sizeof(global_dyn_stats[i].sum)) !=
        sizeof(global_dyn_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *)&global_dyn_stats[i].count, sizeof(global_dyn_stats[i].count)) !=
        sizeof(global_dyn_stats[i].count))
      goto Lmissmatch;
  }
  Debug("stats", "read_stats_snap: read statistics");

  // close(fd);
  socketManager.close(fd);
  return;

Lmissmatch:
  Note("clearing statistics");
  clear_stats();
  // close(fd);
  socketManager.close(fd);
}

static void
write_stats_snap()
{
  int fd = 0;
  int version = STATS_MAJOR_VERSION;
  char *buf = NULL;

  if ((fd = open_stats_snap()) < 0) {
    Warning("unable to snap statistics");
    return;
  }

  {
    int stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS + MAX_DYN_STATS - NO_DYN_STATS;
    int buf_size = sizeof(unsigned int) * 3 + stats_size * (sizeof(global_dyn_stats[0].sum) + sizeof(global_dyn_stats[0].count));
    buf = (char *)ats_malloc(buf_size);
    char *p = buf;
    int i = 0;

    memcpy(p, (char *)&version, sizeof(version));
    p += sizeof(version);
    memcpy(p, (char *)&stats_size, sizeof(stats_size));
    p += sizeof(stats_size);

    stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS;
    STAT_LOCK_ACQUIRE(&(global_http_trans_stat_lock));
    for (i = 0; i < stats_size; i++) {
      memcpy(p, (char *)&global_http_trans_stats[i].sum, sizeof(global_http_trans_stats[i].sum));
      p += sizeof(global_http_trans_stats[i].sum);
      memcpy(p, (char *)&global_http_trans_stats[i].count, sizeof(global_http_trans_stats[i].count));
      p += sizeof(global_http_trans_stats[i].count);
    }
    STAT_LOCK_RELEASE(&(global_http_trans_stat_lock));
    stats_size = MAX_DYN_STATS - NO_DYN_STATS;
    for (i = 0; i < stats_size; i++) {
      // INKqa09981 (Clearing Host Database and DNS Statistics)
      ink_statval_t count, sum;
      READ_GLOBAL_DYN_STAT(i, count, sum);
      memcpy(p, (char *)&sum, sizeof(sum));
      p += sizeof(sum);
      memcpy(p, (char *)&count, sizeof(count));
      p += sizeof(count);
    }
    memcpy(p, (char *)&version, sizeof(version));

    if (socketManager.write(fd, buf, buf_size) != buf_size) {
      Warning("unable to snap statistics");
      ats_free(buf);
      socketManager.close(fd);
      return;
    }
  }
  ats_free(buf);
  socketManager.close(fd);
  Debug("stats", "snapped stats");
}

struct SnapStatsContinuation : public Continuation {
  int
  mainEvent(int /* event ATS_UNUSED */, Event *e ATS_UNUSED)
  {
    write_stats_snap();
    e->schedule_every(HRTIME_SECONDS(snap_stats_every));
    return EVENT_CONT;
  }
  SnapStatsContinuation() : Continuation(new_ProxyMutex()) { SET_HANDLER(&SnapStatsContinuation::mainEvent); }
};

static void
take_rusage_snap()
{
  rusage_snap_old = rusage_snap;
  rusage_snap_time_old = rusage_snap_time;
  int retries = 3;
  while (retries--) {
    if (getrusage(RUSAGE_SELF, &rusage_snap) < 0) {
      if (errno == EINTR)
        continue;
      Note("getrusage [%d %s]", errno, strerror(errno));
    } else
      rusage_snap_time = Thread::get_hrtime();
    break;
  }
  Debug("rusage", "took rusage snap %" PRId64 "", rusage_snap_time);
}

struct SnapCont;
typedef int (SnapCont::*SnapContHandler)(int, void *);

struct SnapCont : public Continuation {
  int
  mainEvent(int /* event ATS_UNUSED */, Event *e)
  {
    take_rusage_snap();
    e->schedule_every(SNAP_USAGE_PERIOD);
    return EVENT_CONT;
  }
  SnapCont(ProxyMutex *m) : Continuation(m) { SET_HANDLER((SnapContHandler)&SnapCont::mainEvent); }
};

void
start_stats_snap()
{
  eventProcessor.schedule_every(new SnapCont(rusage_snap_mutex), SNAP_USAGE_PERIOD, ET_CALL);
  if (snap_stats_every)
    eventProcessor.schedule_every(new SnapStatsContinuation(), HRTIME_SECONDS(snap_stats_every), ET_CALL);
  else
    Warning("disabling statistics snap");
}

static Action *
stat_callback(Continuation *cont, HTTPHdr *header)
{
  URL *url;
  int length;
  const char *path;
  char *result = NULL;
  int result_size;
  bool empty;

  url = header->url_get();
  path = url->path_get(&length);

  char *buffer = NULL;
  int buffer_len = 0;
  int num_prefix_buffer;

  char *var_prefix = (char *)alloca((length + 1) * sizeof(char));

  memset(var_prefix, 0, ((length + 1) * sizeof(char)));
  if (path && length > 0)
    ink_strlcpy(var_prefix, path, length + 1);

  num_prefix_buffer = RecGetRecordPrefix_Xmalloc(var_prefix, &buffer, &buffer_len);
  empty = (num_prefix_buffer == 0);

  if (!empty) {
    result_size = (buffer_len + 16) * sizeof(char);
    result = (char *)ats_malloc(result_size);
    memset(result, 0, result_size);

    snprintf(result, result_size - 7, "<pre>\n%s", buffer);
  }


  if (!empty) {
    StatPageData data;

    ink_strlcat(result, "</pre>\n", result_size);

    data.data = result;
    data.length = strlen(result);
    cont->handleEvent(STAT_PAGE_SUCCESS, &data);
  } else {
    ats_free(result);
    cont->handleEvent(STAT_PAGE_FAILURE, NULL);
  }
  ats_free(buffer);

  return ACTION_RESULT_DONE;
}

static Action *
testpage_callback(Continuation *cont, HTTPHdr *)
{
  const int buf_size = 64000;
  char *buffer = (char *)ats_malloc(buf_size);

  for (int i = 0; i < buf_size; i++) {
    buffer[i] = (char)('a' + (i % 26));
  }
  buffer[buf_size - 1] = '\0';

  StatPageData data;

  data.data = buffer;
  data.length = strlen(buffer);
  cont->handleEvent(STAT_PAGE_SUCCESS, &data);

  return ACTION_RESULT_DONE;
}

static void
testpage_callback_init()
{
  statPagesManager.register_http("test", testpage_callback);
}

void
initialize_all_global_stats()
{
  int istat, i;
  char snap_file[PATH_NAME_MAX];
  ats_scoped_str rundir(RecConfigReadRuntimeDir());

  if (access(rundir, R_OK | W_OK) == -1) {
    Warning("Unable to access() local state directory '%s': %d, %s", (const char *)rundir, errno, strerror(errno));
    Warning(" Please set 'proxy.config.local_state_dir' to allow statistics collection");
  }
  REC_ReadConfigString(snap_file, "proxy.config.stats.snap_file", PATH_NAME_MAX);
  Layout::relative_to(snap_filename, sizeof(snap_filename), (const char *)rundir, snap_file);
  Debug("stats", "stat snap filename %s", snap_filename);

  statPagesManager.register_http("stat", stat_callback);

  testpage_callback_init();

  read_stats_snap();
  rusage_snap_mutex = new_ProxyMutex();
  take_rusage_snap();
  take_rusage_snap(); // fill in _old as well

  STAT_LOCK_INIT(&(global_http_trans_stat_lock), "Global Http Stats Lock");

  for (istat = NO_HTTP_TRANS_STATS; istat < MAX_HTTP_TRANS_STATS; istat++) {
    if (!persistent_stat(istat)) {
      INITIALIZE_GLOBAL_TRANS_STATS(global_http_trans_stats[istat]);
    }
  }

  for (istat = NO_DYN_STATS; istat < MAX_DYN_STATS; istat++) {
    if (!persistent_stat(istat)) {
      i = istat - DYN_STAT_START;
      INITIALIZE_GLOBAL_DYN_STATS(global_dyn_stats[i], "Dyn Stat Lock");
    }
  }

// TODO: HMMMM, wtf does this do? The following is that this
// function does:
// ink_atomic_swap(&this->f_update_lock, (void *) func)
//
// pmgmt->record_data->registerUpdateLockFunc(tmp_stats_lock_function);

#ifdef DEBUG
  ink_mutex_init(&http_time_lock, "Http Time Function Lock");
  last_http_local_time = 0;
#endif

  clear_http_handler_times();
}
