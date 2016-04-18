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

  ClusterLoadMonitor.h
****************************************************************************/

#ifndef _P_ClusterLoadMonitor_h
#define _P_ClusterLoadMonitor_h

#include "P_Cluster.h"

//***************************************************************************
// ClusterLoadMonitor class -- Compute cluster interconnect load metric
//***************************************************************************
class ClusterLoadMonitor : public Continuation
{
public:
  /////////////////////////////////////
  // Defined by records.config
  /////////////////////////////////////
  static int cf_monitor_enabled;
  static int cf_ping_message_send_msec_interval;
  static int cf_num_ping_response_buckets;
  static int cf_msecs_per_ping_response_bucket;
  static int cf_ping_latency_threshold_msecs;
  static int cf_cluster_load_compute_msec_interval;
  static int cf_cluster_periodic_msec_interval;
  static int cf_ping_history_buf_length;
  static int cf_cluster_load_clear_duration;
  static int cf_cluster_load_exceed_duration;

  struct cluster_load_ping_msg {
    int magicno;
    int version;
    int sequence_number;
    ink_hrtime send_time;
    ClusterLoadMonitor *monitor;

    enum {
      CL_MSG_MAGICNO = 0x12ABCDEF,
      CL_MSG_VERSION = 1,
    };
    cluster_load_ping_msg(ClusterLoadMonitor *m = 0)
      : magicno(CL_MSG_MAGICNO), version(CL_MSG_VERSION), sequence_number(0), send_time(0), monitor(m)
    {
    }
  };

  static void cluster_load_ping_rethandler(ClusterHandler *, void *, int);

public:
  ClusterLoadMonitor(ClusterHandler *ch);
  void init();
  ~ClusterLoadMonitor();
  void cancel_monitor();
  bool is_cluster_overloaded();

private:
  void compute_cluster_load();
  void note_ping_response_time(ink_hrtime, int);
  void recv_cluster_load_msg(cluster_load_ping_msg *);
  void send_cluster_load_msg(ink_hrtime);
  int cluster_load_periodic(int, Event *);

private:
  ////////////////////////////////////////////////////
  // Copy of global configuration (records.config)
  ////////////////////////////////////////////////////
  int ping_message_send_msec_interval;
  int num_ping_response_buckets;
  int msecs_per_ping_response_bucket;
  int ping_latency_threshold_msecs;
  int cluster_load_compute_msec_interval;
  int cluster_periodic_msec_interval;
  int ping_history_buf_length;
  int cluster_load_clear_duration;
  int cluster_load_exceed_duration;

  // Class specific data
  ClusterHandler *ch;
  int *ping_response_buckets;
  ink_hrtime *ping_response_history_buf;
  int ping_history_buf_head;
  Action *periodic_action;

  int cluster_overloaded;
  int cancel_periodic;
  ink_hrtime last_ping_message_sent;
  ink_hrtime last_cluster_load_compute;
  int cluster_load_msg_sequence_number;
  int cluster_load_msg_start_sequence_number;
};

#endif /* _ClusterLoadMonitor_h */
