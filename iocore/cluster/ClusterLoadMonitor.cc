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

  ClusterLoadMonitor.cc
****************************************************************************/

#include "P_Cluster.h"
int ClusterLoadMonitor::cf_monitor_enabled;
int ClusterLoadMonitor::cf_ping_message_send_msec_interval;
int ClusterLoadMonitor::cf_num_ping_response_buckets;
int ClusterLoadMonitor::cf_msecs_per_ping_response_bucket;
int ClusterLoadMonitor::cf_ping_latency_threshold_msecs;
int ClusterLoadMonitor::cf_cluster_load_compute_msec_interval;
int ClusterLoadMonitor::cf_cluster_periodic_msec_interval;
int ClusterLoadMonitor::cf_ping_history_buf_length;
int ClusterLoadMonitor::cf_cluster_load_clear_duration;
int ClusterLoadMonitor::cf_cluster_load_exceed_duration;

ClusterLoadMonitor::ClusterLoadMonitor(ClusterHandler *ch)
  : Continuation(0),
    ch(ch),
    ping_history_buf_head(0),
    periodic_action(0),
    cluster_overloaded(0),
    cancel_periodic(0),
    cluster_load_msg_sequence_number(0),
    cluster_load_msg_start_sequence_number(0)
{
  mutex = this->ch->mutex;
  SET_HANDLER(&ClusterLoadMonitor::cluster_load_periodic);

  ping_message_send_msec_interval = cf_ping_message_send_msec_interval ? cf_ping_message_send_msec_interval : 100;
  Debug("cluster_monitor", "ping_message_send_msec_interval=%d", ping_message_send_msec_interval);

  num_ping_response_buckets = cf_num_ping_response_buckets ? cf_num_ping_response_buckets : 100;
  Debug("cluster_monitor", "num_ping_response_buckets=%d", num_ping_response_buckets);

  msecs_per_ping_response_bucket = cf_msecs_per_ping_response_bucket ? cf_msecs_per_ping_response_bucket : 50;
  Debug("cluster_monitor", "msecs_per_ping_response_bucket=%d", msecs_per_ping_response_bucket);

  ping_latency_threshold_msecs = cf_ping_latency_threshold_msecs ? cf_ping_latency_threshold_msecs : 500;
  Debug("cluster_monitor", "ping_latency_threshold_msecs=%d", ping_latency_threshold_msecs);

  cluster_load_compute_msec_interval = cf_cluster_load_compute_msec_interval ? cf_cluster_load_compute_msec_interval : 5000;
  Debug("cluster_monitor", "cluster_load_compute_msec_interval=%d", cluster_load_compute_msec_interval);

  cluster_periodic_msec_interval = cf_cluster_periodic_msec_interval ? cf_cluster_periodic_msec_interval : 100;
  Debug("cluster_monitor", "cluster_periodic_msec_interval=%d", cluster_periodic_msec_interval);

  ping_history_buf_length = cf_ping_history_buf_length ? cf_ping_history_buf_length : 120;
  Debug("cluster_monitor", "ping_history_buf_length=%d", ping_history_buf_length);

  cluster_load_clear_duration = cf_cluster_load_clear_duration ? cf_cluster_load_clear_duration : 24;
  Debug("cluster_monitor", "cluster_load_clear_duration=%d", cluster_load_clear_duration);

  cluster_load_exceed_duration = cf_cluster_load_exceed_duration ? cf_cluster_load_exceed_duration : 4;
  Debug("cluster_monitor", "cluster_load_exceed_duration=%d", cluster_load_exceed_duration);

  int nbytes            = sizeof(int) * num_ping_response_buckets;
  ping_response_buckets = (int *)ats_malloc(nbytes);
  memset((char *)ping_response_buckets, 0, nbytes);

  nbytes                    = sizeof(ink_hrtime) * ping_history_buf_length;
  ping_response_history_buf = (ink_hrtime *)ats_malloc(nbytes);
  memset((char *)ping_response_history_buf, 0, nbytes);

  last_ping_message_sent    = HRTIME_SECONDS(0);
  last_cluster_load_compute = HRTIME_SECONDS(0);
}

void
ClusterLoadMonitor::init()
{
  periodic_action = eventProcessor.schedule_every(this, HRTIME_MSECONDS(cluster_periodic_msec_interval), ET_CALL);
}

ClusterLoadMonitor::~ClusterLoadMonitor()
{
  //
  // Note: Since the ClusterLoadMonitor is only associated
  //       with the ClusterHandler, a periodic callback operating
  //       on a freed ClusterLoadMonitor is not possible, since the
  //       ClusterHandler is only deleted after several minutes.  Allowing
  //       plenty of time for the periodic to cancel itself via the
  //       "cancel_periodic" flag.
  //
  ink_release_assert(!periodic_action);
  if (ping_response_buckets) {
    ats_free(ping_response_buckets);
    ping_response_buckets = 0;
  }
  if (ping_response_history_buf) {
    ats_free(ping_response_history_buf);
    ping_response_history_buf = 0;
  }
}

void
ClusterLoadMonitor::cancel_monitor()
{
  if (!cancel_periodic)
    cancel_periodic = 1;
}

bool
ClusterLoadMonitor::is_cluster_overloaded()
{
  return (cluster_overloaded ? true : false);
}

void
ClusterLoadMonitor::compute_cluster_load()
{
  // Compute ping message latency by scanning the response time
  // buckets and averaging the results.

  int n;
  int sum      = 0;
  int entries  = 0;
  int n_bucket = 0;

  for (n = 0; n < num_ping_response_buckets; ++n) {
    if (ping_response_buckets[n]) {
      entries += ping_response_buckets[n];
      sum += (ping_response_buckets[n] * (n + 1));
    }
    ping_response_buckets[n] = 0;
  }
  if (entries) {
    n_bucket = sum / entries;
  } else {
    n_bucket = 1;
  }
  ink_hrtime current_ping_latency = HRTIME_MSECONDS(n_bucket * msecs_per_ping_response_bucket);

  // Invalidate messages associated with this sample interval
  cluster_load_msg_start_sequence_number = cluster_load_msg_sequence_number;

  // Log ping latency in history buffer.

  ping_response_history_buf[ping_history_buf_head++] = current_ping_latency;
  ping_history_buf_head                              = ping_history_buf_head % ping_history_buf_length;

  // Determine the current state of the cluster interconnect using
  // the configured limits.  We determine the state as follows.
  //   if (cluster overloaded)
  //     Determine if it is still in the overload state by examining
  //     the last 'cluster_load_clear_duration' entries in the history
  //     buffer and declaring it not overloaded if none of the entries
  //     exceed the threshold.
  //   else
  //     Determine if it is now in the overload state by examining
  //     the last 'cluster_load_exceed_duration' entries in the history
  //     buffer and declaring it overloaded if all of the entries
  //     exceed the threshold.

  int start, end;
  ink_hrtime ping_latency_threshold = HRTIME_MSECONDS(ping_latency_threshold_msecs);

  start = ping_history_buf_head - 1;
  if (start < 0)
    start += ping_history_buf_length;
  end = start;

  if (cluster_overloaded) {
    end -= (cluster_load_clear_duration <= ping_history_buf_length ? cluster_load_clear_duration : ping_history_buf_length);
  } else {
    end -= (cluster_load_exceed_duration <= ping_history_buf_length ? cluster_load_exceed_duration : ping_history_buf_length);
  }
  if (end < 0)
    end += ping_history_buf_length;

  int threshold_clear    = 0;
  int threshold_exceeded = 0;
  do {
    if (ping_response_history_buf[start] >= ping_latency_threshold)
      ++threshold_exceeded;
    else
      ++threshold_clear;
    if (--start < 0)
      start = start + ping_history_buf_length;
  } while (start != end);

  if (cluster_overloaded) {
    if (threshold_exceeded == 0)
      cluster_overloaded = 0;
  } else {
    if (threshold_exceeded && (threshold_clear == 0))
      cluster_overloaded = 1;
  }
  Debug("cluster_monitor", "[%u.%u.%u.%u] overload=%d, clear=%d, exceed=%d, latency=%d", DOT_SEPARATED(this->ch->machine->ip),
        cluster_overloaded, threshold_clear, threshold_exceeded, n_bucket);
}

void
ClusterLoadMonitor::note_ping_response_time(ink_hrtime response_time, int sequence_number)
{
#ifdef CLUSTER_TOMCAT
  ProxyMutex *mutex = this->ch->mutex; // hack for stats
#endif

  CLUSTER_SUM_DYN_STAT(CLUSTER_PING_TIME_STAT, response_time);
  int bucket = (int)(response_time / HRTIME_MSECONDS(msecs_per_ping_response_bucket));
  Debug("cluster_monitor_ping", "[%u.%u.%u.%u] ping: %d %d", DOT_SEPARATED(this->ch->machine->ip), bucket, sequence_number);

  if (bucket >= num_ping_response_buckets)
    bucket = num_ping_response_buckets - 1;
  ink_atomic_increment(&ping_response_buckets[bucket], 1);
}

void
ClusterLoadMonitor::recv_cluster_load_msg(cluster_load_ping_msg *m)
{
  // We have received back our ping message.
  ink_hrtime now = Thread::get_hrtime();

  if ((now >= m->send_time) &&
      ((m->sequence_number >= cluster_load_msg_start_sequence_number) && (m->sequence_number < cluster_load_msg_sequence_number))) {
    // Valid message, note response time.
    note_ping_response_time(now - m->send_time, m->sequence_number);
  }
}

void
ClusterLoadMonitor::cluster_load_ping_rethandler(ClusterHandler *ch, void *data, int len)
{
  // Global cluster load ping message return handler which
  // dispatches the result to the class specific handler.

  if (ch) {
    if (len == sizeof(struct cluster_load_ping_msg)) {
      struct cluster_load_ping_msg m;
      memcpy((void *)&m, data, len); // unmarshal

      if (m.monitor && (m.magicno == cluster_load_ping_msg::CL_MSG_MAGICNO) &&
          (m.version == cluster_load_ping_msg::CL_MSG_VERSION)) {
        m.monitor->recv_cluster_load_msg(&m);
      }
    }
  }
}

void
ClusterLoadMonitor::send_cluster_load_msg(ink_hrtime current_time)
{
  // Build and send cluster load ping message.

  struct cluster_load_ping_msg m(this);

  m.sequence_number = cluster_load_msg_sequence_number++;
  m.send_time       = current_time;
  cluster_ping(ch, cluster_load_ping_rethandler, (void *)&m, sizeof(m));
}

int
ClusterLoadMonitor::cluster_load_periodic(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  // Perform periodic cluster load computation actions.

  if (cancel_periodic) {
    periodic_action->cancel();
    periodic_action = 0;
    return EVENT_DONE;
  }

  if (!cf_monitor_enabled) {
    return EVENT_CONT;
  }
  // Generate periodic ping messages.

  ink_hrtime current_time = Thread::get_hrtime();
  if ((current_time - last_ping_message_sent) > HRTIME_MSECONDS(ping_message_send_msec_interval)) {
    send_cluster_load_msg(current_time);
    last_ping_message_sent = current_time;
  }
  // Compute cluster load.

  if ((current_time - last_cluster_load_compute) > HRTIME_MSECONDS(cluster_load_compute_msec_interval)) {
    compute_cluster_load();
    last_cluster_load_compute = current_time;
  }
  return EVENT_CONT;
}

// End of ClusterLoadMonitor.cc
