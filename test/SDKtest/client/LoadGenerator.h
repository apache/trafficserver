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

  LoadGenerator.h
******************************************************************/

#ifndef _LoadGenerator_h_
#define _LoadGenerator_h_
#include "Defines.h"
#include "Plugin.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <values.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>

typedef char host_name[MAX_HOSTNAME_SIZE];
struct LoadGenerator
{
  long max_hotset_serial_num;
  long max_docset_serial_num;

  // Config variables
  int debug;
  int ssl;
  int keepalive;                /* how many requests per connection */

  int synthetic;                /* 1 if synthetic testing, 0 if from logfile */
  FILE *url_file;               /* only valid for if synthetic = 0 */

  /* Rest of the parameters only valid if synthetic = 1 */
  long warmup;
  int num_origin_servers;
  double docset;
  double hotset;
  double hotset_access_ratio;
  host_name *origin_server_names;
  char **origin_server_ports;
  char *target_host;
  char *target_port;
  char *document_base;
  struct sockaddr_in target_addr[MAX_ORIGIN_SERVERS];

  int num_sizes;                /* Number of sizes in the docsize distribution */
  long *sizes;                  /* actual sizes */
  int direct;
  TSPlugin *plug_in;
  double *cumulative_size_prob;
  /* Cumulative probability of selecting different sizes
     cumulative_size_prob[num_sizes-1] must be 1.0 */

  // Stats
  long hotset_generated;
  /* number of documents generated from hotset */
  long random_generated;
  /* number of docs generated from outside hotset */
  long generated_set;           /* Total number of docs generated */
  long generated_size;          /* Total number of docs generated */
  long size_generated[MAX_SIZES];       /* Number of docs of each size */
  long generated_origin_servers;        /* Total number of docs generated */
  long origin_servers_generated[MAX_ORIGIN_SERVERS];
  /*Number of docs for each server */
  void generate_size_str(char *size_str, long *size_requested_p);
  void generate_serial_number_str(char *serial_number_str);
  void generate_origin_server_target(char *origin_server_str, struct sockaddr_in **target);
  void create_synthetic_request(char *req_string, void **req_id, long *size_requested_p, struct sockaddr_in **target);
  void create_request_from_logfile(char *req_string, long *size_requested_p);
  void generate_new_request(char *req_string, void **req_id, long *size_requested_p, struct sockaddr_in **target);
  void generate_dynamic_origin_server_target(char *hostname, char *portname, struct sockaddr_in **target);
  void print_stats();
  void initialize_stats();
  void initialize_targets();

    LoadGenerator(FILE * aurl_file,     /* only valid for if synthetic = 0 */
                  int akeepalive,       /* how many requests per connection */
                  TSPlugin * aplug_in)
  {
    warmup = 0;
    synthetic = 0;
    url_file = aurl_file;
    keepalive = akeepalive;
    plug_in = aplug_in;
  }
  LoadGenerator(int akeepalive,
                long awarmup,
                int adebug,
                int assl,
                int anum_origin_servers,
                double adocset,
                double ahotset,
                double ahotset_access_ratio,
                char aorigin_server_names[][MAX_HOSTNAME_SIZE],
                char *aorigin_server_ports[MAX_HOSTNAME_SIZE],
                char *atarget_host, char *atarget_port, char *adocument_base,
//    struct sockaddr_in atarget_addr[],
                int anum_sizes, /* Number of sizes in the docsize distribution */
                long *asizes,   /* actual sizes */
                double *acumulative_size_prob,
                /* Cumulative probability of selecting different sizes
                   cumulative_size_prob[num_sizes-1] must be 1.0 */
                int adirect, TSPlugin * aplug_in)
  {
    plug_in = aplug_in;
    synthetic = 1;
    keepalive = akeepalive;
    warmup = awarmup;
    debug = adebug;
    ssl = assl;
    num_origin_servers = anum_origin_servers;
    docset = adocset;
    hotset = ahotset;
    hotset_access_ratio = ahotset_access_ratio;
    origin_server_names = aorigin_server_names;
    origin_server_ports = aorigin_server_ports;
    target_host = atarget_host;
    target_port = atarget_port;
//    target_addr = atarget_addr;
    document_base = adocument_base;
    num_sizes = anum_sizes;
    sizes = asizes;
    cumulative_size_prob = acumulative_size_prob;
    direct = adirect;
    max_hotset_serial_num = (long) (hotset / (num_origin_servers * num_sizes));
    max_docset_serial_num = (long) (docset / (num_origin_servers * num_sizes));
    initialize_stats();
    initialize_targets();
  }
  ~LoadGenerator() {
  }
};
#endif // #ifndef _LoadGenerator_h_
