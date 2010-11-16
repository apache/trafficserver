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

/*************************** -*- Mod: C++ -*- *********************

  Config.h
******************************************************************/

#ifndef _Config_h_
#define _Config_h_
#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include "Defines.h"
#include "Plugin.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

struct Config
{
  char target_host[MAX_HOSTNAME_SIZE];
  /* who is going to receive the reqests */
  char target_port[MAX_PORTNAME_SIZE];
  /* at which it is going to receive the reqests */
  char document_base[MAX_ONEREQUESTSTR_SIZE];
  char origin_server_names[MAX_ORIGIN_SERVERS][MAX_HOSTNAME_SIZE];
  char *origin_server_ports[MAX_ORIGIN_SERVERS];
  char log_file[MAX_FILENAME_SIZE];
  /* File containing the logs (if synthetic=0) */
  char docsize_dist_file[MAX_FILENAME_SIZE];
  /* File that has document size distribution */
  char thinktime_dist_file[MAX_FILENAME_SIZE];
  /* File that has think time distribution */
  char target_byterate_dist_file[MAX_FILENAME_SIZE];
  /* File that has target_byterate distribution */
  FILE *conf_file, *log_file_p, *docsize_dist_file_p, *thinktime_dist_file_p, *target_byterate_dist_file_p;
  TSPlugin *plug_in;
  int direct;
  int synthetic;
  int execution_interval;
  int reporting_interval;
  double histogram_max;
  double histogram_resolution;
  long round_trip_time_cutoff;
  long first_byte_latency_cutoff;
  long connect_time_cutoff;
  int debug;
  int ssl;
  int keepalive;
  int num_origin_servers;
  int users;
  int read_timeout;             /* poll timeout in mseconds */
  double hotset;
  double hotset_access_ratio;
  double docset;
  int hitrate;
  long docsize_size_sum;
  int num_sizes;
  int QOS_docsize;
  long sizes[MAX_SIZES];
  double cumulative_size_prob[MAX_SIZES];
  void read_docsize_dist(long warmup);
  int num_thinktimes;
  long thinktimes[MAX_THTSTIMES];
  double cumulative_thinktime_prob[MAX_THTSTIMES];
  int num_target_byterates;
  long target_byterates[MAX_TARGET_BYTERATES];
  double cumulative_target_byterate_prob[MAX_TARGET_BYTERATES];
  void read_thinktime_dist();
  void read_target_byterate_dist();
    Config(long warmup, char *config_file, TSPlugin * aplug_in, int rd_tout);
};

#endif // #ifndef _Config_h_
