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
#include "BaseRecords.h"
#include "ProcessManager.h"
#include "Config.h"
#include "StatPages.h"
#include "HTTP.h"
#include "RecordsConfig.h"

// defines

#define SNAP_USAGE_PERIOD         HRTIME_SECONDS(2)


// variables

#ifdef DEBUG
ink_mutex http_time_lock;
time_t last_http_local_time;
#endif
ink_stat_lock_t global_http_trans_stat_lock;
ink_unprot_global_stat_t global_http_trans_stats[MAX_HTTP_TRANS_STATS];
ink_stat_lock_t global_nntp_trans_stat_lock;
ink_unprot_global_stat_t global_nntp_trans_stats[MAX_NNTP_TRANS_STATS];
inkcoreapi ink_stat_lock_t global_rni_trans_stat_lock;
inkcoreapi ink_unprot_global_stat_t global_rni_trans_stats[MAX_RNI_TRANS_STATS];
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


char snap_filename[PATH_NAME_MAX+1] = DEFAULT_SNAP_FILENAME;

#define DEFAULT_PERSISTENT

#ifndef DEFAULT_PERSISTENT
static int persistent_stats[] = {
  http_incoming_requests_stat
};
#else
static int non_persistent_stats[] = {
  nntp_client_connections_currently_open_stat,
  nntp_server_connections_currently_open_stat,
  nntp_cache_connections_currently_open_stat,
  ////////////////////////////
  // Start of Cluster stats
  ////////////////////////////
  cluster_connections_open_stat,
  cluster_connections_openned_stat,
  cluster_con_total_time_stat,
  cluster_ctrl_msgs_sent_stat,
  cluster_slow_ctrl_msgs_sent_stat,
  cluster_ctrl_msgs_recvd_stat,
  cluster_slow_ctrl_msgs_recvd_stat,
  cluster_ctrl_msgs_send_time_stat,
  cluster_ctrl_msgs_recv_time_stat,
  cluster_read_bytes_stat,
  cluster_write_bytes_stat,
  cluster_op_delayed_for_lock_stat,
  cluster_connections_locked_stat,
  cluster_connections_bumped_stat,
  cluster_nodes_stat,
  cluster_net_backup_stat,
  cluster_machines_allocated_stat,
  cluster_machines_freed_stat,
  cluster_configuration_changes_stat,
  cluster_delayed_reads_stat,
  cluster_byte_bank_used_stat,
  cluster_alloc_data_news_stat,
  cluster_write_bb_mallocs_stat,
  cluster_partial_reads_stat,
  cluster_partial_writes_stat,
  cluster_cache_outstanding_stat,
  cluster_remote_op_timeouts_stat,
  cluster_remote_op_reply_timeouts_stat,
  cluster_chan_inuse_stat,
  cluster_open_delays_stat,
  cluster_open_delay_time_stat,
  cluster_cache_callbacks_stat,
  cluster_cache_callback_time_stat,
  cluster_cache_rmt_callbacks_stat,
  cluster_cache_rmt_callback_time_stat,
  cluster_cache_lkrmt_callbacks_stat,
  cluster_cache_lkrmt_callback_time_stat,
  cluster_thread_steal_expires_stat,
  cluster_local_connections_closed_stat,
  cluster_local_connection_time_stat,
  cluster_remote_connections_closed_stat,
  cluster_remote_connection_time_stat,
  cluster_rdmsg_assemble_time_stat,
  cluster_ping_time_stat,
  cluster_setdata_no_clustervc_stat,
  cluster_setdata_no_tunnel_stat,
  cluster_setdata_no_cachevc_stat,
  cluster_setdata_no_cluster_stat,
  cluster_vc_write_stall_stat,
  cluster_no_remote_space_stat,
  cluster_level1_bank_stat,
  cluster_multilevel_bank_stat,
  cluster_vc_cache_insert_lock_misses_stat,
  cluster_vc_cache_inserts_stat,
  cluster_vc_cache_lookup_lock_misses_stat,
  cluster_vc_cache_lookup_hits_stat,
  cluster_vc_cache_lookup_misses_stat,
  cluster_vc_cache_scans_stat,
  cluster_vc_cache_scan_lock_misses_stat,
  cluster_vc_cache_purges_stat,
  cluster_write_lock_misses_stat,
  /////////////////////////////////////
  // Start of Scheduled Update stats
  /////////////////////////////////////
  // WMT stats
  // DNS
  //dns_success_time_stat
};
#endif



#define _HEADER \
RniTransactionStatsString_t RniTransactionStatsStrings[] = {

#define _FOOTER };
#define _D(_x) { _x, #_x},

#include "RniTransStats.h"
#undef _HEADER
#undef _FOOTER
#undef _D


#define _HEADER \
DynamicStatsString_t DynamicStatsStrings[] = {

#define _FOOTER };
#define _D(_x) { _x, #_x },

#include "DynamicStats.h"
#undef _HEADER
#undef _FOOTER
#undef _D



// functions

static int
persistent_stat(int i)
{
#ifndef DEFAULT_PERSISTENT
  for (int j = 0; j < (int) SIZE(persistent_stats); j++)
    if (persistent_stats[j] == i)
      return 1;
  return 0;
#else
  for (int j = 0; j < (int) SIZE(non_persistent_stats); j++)
    if (non_persistent_stats[j] == i)
      return 0;
  return 1;
#endif
}

static int
open_stats_snap()
{
  int fd = socketManager.open(snap_filename,
                              O_CREAT | O_RDWR | _O_ATTRIB_NORMAL);
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
  stats_size = MAX_NNTP_TRANS_STATS - NO_NNTP_TRANS_STATS - 1;
  for (i = 0; i < stats_size; i++) {
    if (persistent_stat(i + NO_NNTP_TRANS_STATS)) {
      global_nntp_trans_stats[i].sum = 0;
      global_nntp_trans_stats[i].count = 0;
    }
  }
  stats_size = MAX_RNI_TRANS_STATS - NO_RNI_TRANS_STATS - 1;
  for (i = 0; i < stats_size; i++) {
    if (persistent_stat(i + NO_RNI_TRANS_STATS)) {
      global_rni_trans_stats[i].sum = 0;
      global_rni_trans_stats[i].count = 0;
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
  if (socketManager.read(fd, (char *) &version_read, sizeof(version_read))
      != sizeof(version_read))
    goto Lmissmatch;
  if (version != version_read)
    goto Lmissmatch;
  stats_size =
    MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS +
    MAX_NNTP_TRANS_STATS - NO_NNTP_TRANS_STATS +
    MAX_RNI_TRANS_STATS - NO_RNI_TRANS_STATS + MAX_DYN_STATS - NO_DYN_STATS;
  if (socketManager.read(fd, (char *) &count, sizeof(count)) != sizeof(count))
    goto Lmissmatch;
  if (count != stats_size)
    goto Lmissmatch;

  stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *) &global_http_trans_stats[i].sum, sizeof(global_http_trans_stats[i].sum))
        != sizeof(global_http_trans_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *) &global_http_trans_stats[i].count, sizeof(global_http_trans_stats[i].count))
        != sizeof(global_http_trans_stats[i].count))
      goto Lmissmatch;
  }
  stats_size = MAX_NNTP_TRANS_STATS - NO_NNTP_TRANS_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *) &global_nntp_trans_stats[i].sum, sizeof(global_nntp_trans_stats[i].sum))
        != sizeof(global_nntp_trans_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *) &global_nntp_trans_stats[i].count, sizeof(global_nntp_trans_stats[i].count))
        != sizeof(global_nntp_trans_stats[i].count))
      goto Lmissmatch;
  }
  stats_size = MAX_RNI_TRANS_STATS - NO_RNI_TRANS_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *) &global_rni_trans_stats[i].sum, sizeof(global_rni_trans_stats[i].sum))
        != sizeof(global_rni_trans_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *) &global_rni_trans_stats[i].count, sizeof(global_rni_trans_stats[i].count))
        != sizeof(global_rni_trans_stats[i].count))
      goto Lmissmatch;
  }
  stats_size = MAX_DYN_STATS - NO_DYN_STATS;
  for (i = 0; i < stats_size; i++) {
    if (socketManager.read(fd, (char *) &global_dyn_stats[i].sum, sizeof(global_dyn_stats[i].sum))
        != sizeof(global_dyn_stats[i].sum))
      goto Lmissmatch;
    if (socketManager.read(fd, (char *) &global_dyn_stats[i].count, sizeof(global_dyn_stats[i].count))
        != sizeof(global_dyn_stats[i].count))
      goto Lmissmatch;
  }
  Debug("stats", "read_stats_snap: read statistics");

  // close(fd);
  socketManager.close(fd, keFile);
  return;

Lmissmatch:
  Note("clearing statistics");
  clear_stats();
  //close(fd);
  socketManager.close(fd, keFile);
}

static void
write_stats_snap()
{
  int fd = 0;
  int version = STATS_MAJOR_VERSION;
  char *buf = NULL;

  if ((fd = open_stats_snap()) < 0)
    goto Lerror;

  {
    int stats_size =
      MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS +
      MAX_NNTP_TRANS_STATS - NO_NNTP_TRANS_STATS +
      MAX_RNI_TRANS_STATS - NO_RNI_TRANS_STATS + MAX_DYN_STATS - NO_DYN_STATS;
    int buf_size = sizeof(unsigned int) * 3 +
      stats_size * (sizeof(global_dyn_stats[0].sum) + sizeof(global_dyn_stats[0].count));
    buf = (char *) xmalloc(buf_size);
    char *p = buf;
    int i = 0;

    memcpy(p, (char *) &version, sizeof(version));
    p += sizeof(version);
    memcpy(p, (char *) &stats_size, sizeof(stats_size));
    p += sizeof(stats_size);

    stats_size = MAX_HTTP_TRANS_STATS - NO_HTTP_TRANS_STATS;
    STAT_LOCK_ACQUIRE(&(global_http_trans_stat_lock));
    for (i = 0; i < stats_size; i++) {
      memcpy(p, (char *) &global_http_trans_stats[i].sum, sizeof(global_http_trans_stats[i].sum));
      p += sizeof(global_http_trans_stats[i].sum);
      memcpy(p, (char *) &global_http_trans_stats[i].count, sizeof(global_http_trans_stats[i].count));
      p += sizeof(global_http_trans_stats[i].count);
    }
    STAT_LOCK_RELEASE(&(global_http_trans_stat_lock));
    stats_size = MAX_NNTP_TRANS_STATS - NO_NNTP_TRANS_STATS;
    STAT_LOCK_ACQUIRE(&(global_nntp_trans_stat_lock));
    for (i = 0; i < stats_size; i++) {
      memcpy(p, (char *) &global_nntp_trans_stats[i].sum, sizeof(global_nntp_trans_stats[i].sum));
      p += sizeof(global_nntp_trans_stats[i].sum);
      memcpy(p, (char *) &global_nntp_trans_stats[i].count, sizeof(global_nntp_trans_stats[i].count));
      p += sizeof(global_nntp_trans_stats[i].count);
    }
    STAT_LOCK_RELEASE(&(global_nntp_trans_stat_lock));
    stats_size = MAX_RNI_TRANS_STATS - NO_RNI_TRANS_STATS;
    STAT_LOCK_ACQUIRE(&(global_rni_trans_stat_lock));
    for (i = 0; i < stats_size; i++) {
      memcpy(p, (char *) &global_rni_trans_stats[i].sum, sizeof(global_rni_trans_stats[i].sum));
      p += sizeof(global_rni_trans_stats[i].sum);
      memcpy(p, (char *) &global_rni_trans_stats[i].count, sizeof(global_rni_trans_stats[i].count));
      p += sizeof(global_rni_trans_stats[i].count);
    }
    STAT_LOCK_RELEASE(&(global_rni_trans_stat_lock));
    stats_size = MAX_DYN_STATS - NO_DYN_STATS;
    for (i = 0; i < stats_size; i++) {
      // INKqa09981 (Clearing Host Database and DNS Statistics)
      ink_statval_t count, sum;
      READ_GLOBAL_DYN_STAT(i, count, sum);
      memcpy(p, (char *) &sum, sizeof(sum));
      p += sizeof(sum);
      memcpy(p, (char *) &count, sizeof(count));
      p += sizeof(count);
    }
    memcpy(p, (char *) &version, sizeof(version));
    p += sizeof(version);

    if (socketManager.write(fd, buf, buf_size) != buf_size)
      goto Lerror;
  }
  if (buf)
    xfree(buf);
  //close(fd);
  socketManager.close(fd, keFile);
  Debug("stats", "snapped stats");
  return;
Lerror:
  if (buf)
    xfree(buf);
  Warning("unable to snap statistics");
  //close(fd);
  socketManager.close(fd, keFile);
}

struct SnapStatsContinuation:Continuation
{
  int mainEvent(int event, Event * e)
  {
    write_stats_snap();
    e->schedule_every(HRTIME_SECONDS(snap_stats_every));
    return EVENT_CONT;
  }
  SnapStatsContinuation():Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&SnapStatsContinuation::mainEvent);
  }
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
      rusage_snap_time = ink_get_hrtime();
    break;
  }
  Debug("rusage", "took rusage snap %d", rusage_snap_time);
}

struct SnapCont;
typedef int (SnapCont::*SnapContHandler) (int, void *);
struct SnapCont:Continuation
{
  int mainEvent(int event, Event * e)
  {
    init_hrtime_basis();
    take_rusage_snap();
    e->schedule_every(SNAP_USAGE_PERIOD);
    return EVENT_CONT;
  }
  SnapCont(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER((SnapContHandler) & SnapCont::mainEvent);
  }
};

void
start_stats_snap()
{
  eventProcessor.schedule_every(NEW(new SnapCont(rusage_snap_mutex)), SNAP_USAGE_PERIOD, ET_CALL);
  if (snap_stats_every)
    eventProcessor.schedule_every(NEW(new SnapStatsContinuation()), HRTIME_SECONDS(snap_stats_every), ET_CALL);
  else
    Warning("disabling statistics snap");
}



static char **stat_names = NULL;
static int nstat_names = 0;

static Action *
stat_callback(Continuation * cont, HTTPHdr * header)
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

  char *var_prefix = (char *) xmalloc((length + 1) * sizeof(char));
  memset(var_prefix, 0, ((length + 1) * sizeof(char)));
  strncpy(var_prefix, path, length);

  num_prefix_buffer = RecGetRecordPrefix_Xmalloc(var_prefix, &buffer, &buffer_len);
  empty = (num_prefix_buffer == 0);
  xfree(var_prefix);

  if (!empty) {

    result_size = (buffer_len + 16) * sizeof(char);
    result = (char *) xmalloc(result_size);
    memset(result, 0, result_size);

    snprintf(result, result_size - 7, "<pre>\n%s", buffer);
  }


  if (!empty) {
    StatPageData data;

    strncat(result, "</pre>\n", result_size - strlen(result) - 1);

    data.data = result;
    data.length = strlen(result);
    cont->handleEvent(STAT_PAGE_SUCCESS, &data);
  } else {
    if (result) {
      xfree(result);
    }
    cont->handleEvent(STAT_PAGE_FAILURE, NULL);
  }

  if (buffer)
    xfree(buffer);

  return ACTION_RESULT_DONE;
}

static void
stat_callback_init()
{

  char *p;
  int i, j;

  statPagesManager.register_http("stat", stat_callback);

  nstat_names = 0;
  for (i = 0; RecordsConfig[i].value_type != INVALID; i++) {
    //if (RecordsConfig[i].type == PROCESS || RecordsConfig[i].type == NODE) {
    nstat_names++;
    //}
  }
  stat_names = (char **) xmalloc(nstat_names * sizeof(char *));

  j = 0;
  for (i = 0; RecordsConfig[i].value_type != INVALID; i++) {
    //if (RecordsConfig[i].type == PROCESS || RecordsConfig[i].type == NODE) {
    stat_names[j] = RecordsConfig[i].name;
    j++;
    //}
  }

  for (i = 1; i < nstat_names; i++) {
    j = i;
    p = stat_names[j];
    while ((j > 0) && (strcmp(p, stat_names[j - 1]) < 0)) {
      stat_names[j] = stat_names[j - 1];
      j -= 1;
    }
    stat_names[j] = p;
  }

}

static Action *
testpage_callback(Continuation * cont, HTTPHdr *)
{
  const int buf_size = 64000;
  char *buffer = (char *) xmalloc(buf_size);

  if (buffer) {
    for (int i = 0; i < buf_size; i++) {
      buffer[i] = (char) ('a' + (i % 26));
    }
    buffer[buf_size - 1] = '\0';

    StatPageData data;

    data.data = buffer;
    data.length = strlen(buffer);
    cont->handleEvent(STAT_PAGE_SUCCESS, &data);
  } else {
    cont->handleEvent(STAT_PAGE_FAILURE, NULL);
  }
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
  char local_state_dir[PATH_NAME_MAX];
  struct stat s;
  int err;

  // Jira TS-21
  REC_ReadConfigString(local_state_dir, "proxy.config.local_state_dir", PATH_NAME_MAX);
  if ((err = stat(local_state_dir, &s)) < 0) {
    Warning("Unable to stat() local state directory '%s': %d %d, %s", local_state_dir, err, errno, strerror(errno));
    Warning(" Please set 'proxy.config.local_state_dir' to allow statistics collection");
  }
  REC_ReadConfigString(snap_file, "proxy.config.stats.snap_file", PATH_NAME_MAX);
  snprintf(snap_filename, sizeof(snap_filename), "%s%s%s", local_state_dir,
           DIR_SEP, snap_file);
  Debug("stats", "stat snap filename %s", snap_filename);

  stat_callback_init();
  testpage_callback_init();

  read_stats_snap();
  rusage_snap_mutex = new_ProxyMutex();
  take_rusage_snap();
  take_rusage_snap();           // fill in _old as well

  STAT_LOCK_INIT(&(global_http_trans_stat_lock), "Global Http Stats Lock");
  STAT_LOCK_INIT(&(global_nntp_trans_stat_lock), "Global Nntp Stats Lock");
  STAT_LOCK_INIT(&(global_rni_trans_stat_lock), "Global Rni Stats Lock");

  for (istat = NO_HTTP_TRANS_STATS; istat < MAX_HTTP_TRANS_STATS; istat++) {
    if (!persistent_stat(istat)) {
      INITIALIZE_GLOBAL_TRANS_STATS(global_http_trans_stats[istat]);
    }
  }

  for (istat = NO_NNTP_TRANS_STATS; istat < MAX_NNTP_TRANS_STATS; istat++) {
    if (!persistent_stat(istat)) {
      INITIALIZE_GLOBAL_TRANS_STATS(global_nntp_trans_stats[istat]);
    }
  }

  for (istat = NO_RNI_TRANS_STATS; istat < MAX_RNI_TRANS_STATS; istat++) {
    if (!persistent_stat(istat)) {
      INITIALIZE_GLOBAL_TRANS_STATS(global_rni_trans_stats[istat]);
    }
  }

  for (istat = NO_DYN_STATS; istat < MAX_DYN_STATS; istat++) {
    if (!persistent_stat(istat)) {
      i = istat - DYN_STAT_START;
      INITIALIZE_GLOBAL_DYN_STATS(global_dyn_stats[i], "Dyn Stat Lock");
    }
  }

  pmgmt->record_data->registerUpdateLockFunc(tmp_stats_lock_function);

#ifdef DEBUG
  ink_mutex_init(&http_time_lock, "Http Time Function Lock");
  last_http_local_time = 0;
#endif

  initialize_http_stats();
  initialize_cache_stats();
}

void *
tmp_stats_lock_function(UpdateLockAction action)
{
  stats_lock_function((void *) (&global_http_trans_stat_lock), action);

  return NULL;
}

void *
stats_lock_function(void *data, UpdateLockAction action)
{
  if (action == UPDATE_LOCK_ACQUIRE) {
    STAT_LOCK_ACQUIRE((ink_stat_lock_t *) data);
  } else {
    STAT_LOCK_RELEASE((ink_stat_lock_t *) data);
  }

  return NULL;
}

void *
dyn_stats_int_msecs_to_float_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);

  float r;
  if (count == 0) {
    r = 0.0;
  } else {
    r = ((float) sum) / 1000.0;
  }
  *(float *) res = r;
  return res;
}

void *
dyn_stats_count_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = count;
  ink_atomic_swap64((ink_statval_t *) res, count);
  return res;
}

void *
dyn_stats_sum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = sum;
  ink_atomic_swap64((ink_statval_t *) res, sum);
  return res;
}

void *
dyn_stats_avg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = (float) sum / (float) count;
  }
  return res;
}

void *
dyn_stats_fsum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);
  *(float *) res = *(double *) &sum;
  return res;
}

void *
dyn_stats_favg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_DYN_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = *(double *) &sum / *(double *) &count;
  }
  return res;
}

void *
dyn_stats_time_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_DYN_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_SECOND;
  }
  *(float *) res = r;
  return res;
}

void *
dyn_stats_time_mseconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_DYN_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_MSECOND;
  }
  *(float *) res = r;
  return res;
}

void *
dyn_stats_time_useconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_DYN_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_USECOND;
  }
  *(float *) res = r;
  return res;
}

// http trans stat functions
// there is the implicit assumption that the lock has
// been acquired.
void *
http_trans_stats_int_msecs_to_float_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);

  float r;
  if (count == 0) {
    r = 0.0;
  } else {
    r = ((float) sum) / 1000.0;
  }
  *(float *) res = r;
  return res;
}

void *
http_trans_stats_count_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = count;
  ink_atomic_swap64((ink_statval_t *) res, count);
  return res;
}

void *
http_trans_stats_sum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = sum;
  ink_atomic_swap64((ink_statval_t *) res, sum);
  return res;
}

void *
http_trans_stats_avg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = (float) sum / (float) count;
  }
  return res;
}

void *
http_trans_stats_fsum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  *(float *) res = *(double *) &sum;
  return res;
}

void *
http_trans_stats_favg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = *(double *) &sum / *(double *) &count;
  }
  return res;
}

void *
http_trans_stats_time_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_SECOND;
  }
  *(float *) res = r;
  return res;
}

void *
http_trans_stats_time_mseconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_MSECOND;
  }
  *(float *) res = r;
  return res;
}

void *
http_trans_stats_time_useconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_HTTP_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_USECOND;
  }
  *(float *) res = r;
  return res;
}

// rni trans stat functions
// there is the implicit assumption that the lock has
// been acquired.
void *
rni_trans_stats_int_msecs_to_float_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);

  float r;
  if (count == 0) {
    r = 0.0;
  } else {
    r = ((float) sum) / 1000.0;
  }
  *(float *) res = r;
  return res;
}

void *
rni_trans_stats_count_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = count;
  ink_atomic_swap64((ink_statval_t *) res, count);
  return res;
}

void *
rni_trans_stats_sum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  //*(ink_statval_t *)res = sum;
  ink_atomic_swap64((ink_statval_t *) res, sum);
  return res;
}

void *
rni_trans_stats_avg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = (float) sum / (float) count;
  }
  return res;
}

void *
rni_trans_stats_fsum_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  *(float *) res = *(double *) &sum;
  return res;
}

void *
rni_trans_stats_favg_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    *(float *) res = 0.0;
  } else {
    *(float *) res = *(double *) &sum / *(double *) &count;
  }
  return res;
}

void *
rni_trans_stats_time_seconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_SECOND;
  }
  *(float *) res = r;
  return res;
}

void *
rni_trans_stats_time_mseconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_MSECOND;
  }
  *(float *) res = r;
  return res;
}

void *
rni_trans_stats_time_useconds_cb(void *data, void *res)
{
  ink_statval_t count, sum;
  float r;
  READ_RNI_TRANS_STAT((long) data, count, sum);
  if (count == 0) {
    r = 0.0;
  } else {
    r = (float) sum / (float) count;
    r = r / (float) HRTIME_USECOND;
  }
  *(float *) res = r;
  return res;
}

void
initialize_http_stats()
{

  ///////////////////////
  // Transaction Stats //
  ///////////////////////

  ///////////////////
  // Dynamic stats //
  ///////////////////
  ////////////////////////////////////////////////////////////////////////////////
  // http - time and count of transactions classified by client's point of view //
  //  the internal stat is in msecs, the output time is float seconds           //
  ////////////////////////////////////////////////////////////////////////////////




  clear_http_handler_times();
}


void
initialize_cache_stats()
{

#ifdef TS_MICRO
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.total_promises",
                                             dyn_stats_count_cb, (void *) stuffer_total_promises);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.total_objects",
                                             dyn_stats_count_cb, (void *) stuffer_total_objects);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.total_bytes_received",
                                             dyn_stats_count_cb, (void *) stuffer_total_bytes_received);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.open_read_blocks",
                                             dyn_stats_count_cb, (void *) stuffer_open_read_blocks);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.ram_cache_hits",
                                             dyn_stats_count_cb, (void *) stuffer_ram_cache_hits);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.url_lookup_misses",
                                             dyn_stats_count_cb, (void *) stuffer_url_lookup_misses);
  pmgmt->record_data->registerStatUpdateFunc("proxy.process.stuffer.total_objects_pushed",
                                             dyn_stats_count_cb, (void *) stuffer_total_objects_pushed);
#endif

}

//////////////////////////////////////////////////////////////////////////////
//
//  TransactionMilestones::TransactionMilestones()
//
//////////////////////////////////////////////////////////////////////////////
TransactionMilestones::TransactionMilestones()
:
ua_begin(0), ua_read_header_done(0), ua_begin_write(0), ua_close(0), server_first_connect(0), server_connect(0),
  // server_connect_end(0),
  // server_begin_write(0),
  server_first_read(0), server_read_header_done(0), server_close(0), cache_open_read_begin(0), cache_open_read_end(0),
  // cache_read_begin(0),
  // cache_read_end(0),
  // cache_open_write_begin(0),
  // cache_open_write_end(0),
  // cache_write_begin(0),
  // cache_write_end(0),
  dns_lookup_begin(0), dns_lookup_end(0), sm_start(0),  // init
  sm_finish(0)                  // kill_this
{
  return;
}

#ifdef DEBUG
bool
TransactionMilestones::invariant()
{
  /*
     bool must_times_set = true;
     bool order_correct = true;

     #define PRINT_COND(cond) { fprintf(stderr,"internal ivry conflict: %s (%s:%d)\n", #cond, __FILE__, __LINE__); }
     #define UNSET_IF_FAILS(v, cond) if (!(cond)) { PRINT_COND(cond); v = false; }
     #define UIF(v, cond) UNSET_IF_FAILS(v, cond);

     UIF(must_times_set, (user_agent_begin != 0));
     UIF(must_times_set, (user_agent_begin_read != 0));
     UIF(must_times_set, (user_agent_read_header_done != 0));
     UIF(must_times_set, (user_agent_close != 0));
     UIF(must_times_set, (state_machine_construct != 0));
     UIF(must_times_set, (state_machine_destruct != 0));

     /////////////////
     // check order //
     /////////////////
     ///////////////////////////
     // user agent milestones //
     ///////////////////////////
     UIF(order_correct, 
     ((user_agent_accept <= user_agent_begin_read) || (user_agent_accept == 0)));
     UIF(order_correct, (user_agent_begin_read <= user_agent_read_header_done));
     UIF(order_correct, 
     ((user_agent_read_header_done <= user_agent_begin_write) || 
     (user_agent_begin_write == 0)));
     UIF(order_correct, (user_agent_read_header_done <= user_agent_close));
     UIF(order_correct, 
     ((user_agent_begin_write <= user_agent_close) || (user_agent_begin_write == 0)));
     /////////////////////////////////
     // user agent  -- data sources //
     /////////////////////////////////
     UIF(order_correct, 
     ((user_agent_begin <= origin_server_open_begin) ||
     (origin_server_open_begin == 0)));
     UIF(order_correct, 
     ((user_agent_begin <= raw_origin_server_connect_begin) ||
     (raw_origin_server_connect_begin == 0)));
     UIF(order_correct,
     ((user_agent_begin <= ftp_server_connect_begin) || 
     (ftp_server_connect_begin == 0)));
     UIF(order_correct,
     ((user_agent_begin <= cache_lookup_begin) || (cache_lookup_begin == 0)));
     UIF(order_correct,
     ((user_agent_begin <= transform_open_begin) || (transform_open_begin == 0)));
     ///////////////////
     // origin server //
     ///////////////////
     UIF(order_correct, (origin_server_open_begin <= origin_server_open_end));
     UIF(order_correct, 
     ((origin_server_open_end <= origin_server_begin_write) ||
     (origin_server_begin_write == 0)));
     UIF(order_correct, 
     ((origin_server_open_end <= origin_server_begin_read) ||
     (origin_server_begin_read == 0)));

     UIF(order_correct, (origin_server_open_end <= origin_server_close));
     UIF(order_correct, (origin_server_begin_read <= origin_server_read_header_done));
     ///////////////////////
     // raw origin server //
     ///////////////////////
     UIF(order_correct, (raw_origin_server_connect_begin <= raw_origin_server_connect_end));
     UIF(order_correct, (raw_origin_server_connect_end <= raw_origin_server_begin_read_write));
     UIF(order_correct, (raw_origin_server_connect_end <= raw_origin_server_close));
     /////////////////
     // ftp server  //
     /////////////////
     UIF(order_correct, (ftp_server_connect_begin <= ftp_server_connect_end));
     UIF(order_correct, 
     ((ftp_server_connect_end <= ftp_server_read_begin) || (ftp_server_read_begin == 0)));
     UIF(order_correct, (ftp_server_read_begin <= ftp_server_read_end));
     ///////////
     // cache //
     ///////////
     UIF(order_correct, ((cache_lookup_begin <= cache_lookup_end) || (cache_lookup_end == 0)));
     UIF(order_correct, ((cache_lookup_end <= cache_update_begin) || (cache_update_begin == 0)));
     UIF(order_correct, (cache_update_begin <= cache_update_end));
     UIF(order_correct, ((cache_lookup_end <= cache_open_read_begin) ||
     (cache_open_read_begin == 0)));
     UIF(order_correct, ((cache_lookup_end <= cache_open_write_begin) ||
     (cache_open_write_begin == 0)));
     UIF(order_correct, (cache_open_read_begin <= cache_open_read_end));
     UIF(order_correct, (cache_open_write_begin <= cache_open_write_end));
     UIF(order_correct, ((cache_open_read_end <= cache_read_begin) ||
     (cache_read_begin == 0)));
     UIF(order_correct, ((cache_open_write_end <= cache_write_begin) ||
     (cache_write_begin == 0)));
     UIF(order_correct, (cache_read_begin <= cache_read_end));
     UIF(order_correct, (cache_write_begin <= cache_write_end));
     ///////////////////
     // transform JG  //
     ///////////////////
     UIF(order_correct, (transform_open_begin <= transform_open_end));
     UIF(order_correct, ((transform_open_end <= transform_close) || (transform_close == 0)));
     ///////////////////
     // state machine //
     ///////////////////
     UIF(order_correct, (state_machine_construct <= state_machine_destruct));

     return (must_times_set && order_correct);
   */

  return true;
}

#endif
