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

/* TestCacheAPI.c
 *
 *
 * Description:
 *   - Simulate the default way of generating requests by the
 *     SDKtest_client with the foll. added options.
 *     . add CacheTester-Pin: <time> to some ratio of requests specified in SDKtest_client.config
 *     . add CacheTester-HostNameSet: 1 to some ratio of requests specified in SDKtest_client.config
 *
 * Added Options in SDKtest_client.config -
 *   pin_ratio : percentage of requests we want to be pinned at the proxy
 *   HostSet_ratio : percentage of requests whose hostname to be set at the proxy
 */

#include "ClientAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#define TRUE 1
#define FALSE 0
#define MAX_SIZES 1000          /* Max number of synthetic doc sizes */
#define MAX_ORIGIN_SERVERS 10   /* Max origin servers */

typedef struct
{
  long doc_size_requested;
  long header_bytes;
} User;

void parse_origin_server_str(char *value);
long generate_serial_number();
long generate_size();
void read_docsize_dist();
int coin_toss(double);

typedef struct
{
  char *target_host;
  char *target_port;
  char *origin_host[MAX_ORIGIN_SERVERS];
  char *origin_port[MAX_ORIGIN_SERVERS];
  int num_origin_server;

  int direct;
  double hotset;
  double docset;
  double hotset_access_ratio;

  long max_hotset_serial_num;
  long max_docset_serial_num;

  /* for document distribution */
  int num_sizes;
  long sizes[MAX_SIZES];
  double cumulative_size_prob[MAX_SIZES];
  FILE *docsize_dist_file_p;

  /* for pin and host_set rates */
  double pin_ratio;
  double hostset_ratio;

  /* for reporting stats */
  long requests;
  long pin_requests;
  long hostset_requests;
  long successful_requests;
  long unfinished_requests;
  long total_bytes_received;
} SDKtestCachePlugin;

SDKtestCachePlugin my_plugin;

void
INKPluginInit(int client_id)
{
  my_plugin.requests = 0;
  my_plugin.successful_requests = 0;

  /* register the plugin functions that are called back
   * later in the program */
  INKFuncRegister(INK_FID_OPTIONS_PROCESS);
  INKFuncRegister(INK_FID_OPTIONS_PROCESS_FINISH);
  INKFuncRegister(INK_FID_CONNECTION_FINISH);
  INKFuncRegister(INK_FID_PLUGIN_FINISH);
  INKFuncRegister(INK_FID_REQUEST_CREATE);
  INKFuncRegister(INK_FID_HEADER_PROCESS);
  INKFuncRegister(INK_FID_PARTIAL_BODY_PROCESS);
  INKFuncRegister(INK_FID_REPORT);
}

/* process options specified in SDKtest_client.config */
void
INKOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "target_host") == 0) {
    my_plugin.target_host = strdup(value);
  } else if (strcmp(option, "target_port") == 0) {
    my_plugin.target_port = strdup(value);
  } else if (strcmp(option, "origin_servers") == 0) {
    parse_origin_server_str(value);
  } else if (strcmp(option, "pin_ratio") == 0) {
    my_plugin.pin_ratio = (double) (atoi(value)) / 100.0;
  } else if (strcmp(option, "hostset_ratio") == 0) {
    my_plugin.hostset_ratio = (double) (atoi(value)) / 100.0;
  } else if (strcmp(option, "hotset") == 0) {
    my_plugin.hotset = atof(value);
    my_plugin.max_hotset_serial_num = (long) my_plugin.hotset;
  } else if (strcmp(option, "docset") == 0) {
    my_plugin.docset = atof(value);
    my_plugin.max_docset_serial_num = (long) my_plugin.docset;
  } else if (strcmp(option, "hitrate") == 0) {
    my_plugin.hotset_access_ratio = (double) (atoi(value)) / 100.0;
  } else if (strcmp(option, "docsize_dist_file") == 0) {
    if (!(my_plugin.docsize_dist_file_p = fopen(value, "r"))) {
      fprintf(stderr, "Error: could not open the docsize_dist_file %s\n", value);
      perror("Error: DocSize Dist File Open");
      exit(1);
    }
  }
}

void
INKOptionsProcessFinish()
{
  read_docsize_dist();
  ((strlen(my_plugin.target_host) == 0) || (strlen(my_plugin.target_port) == 0)) ?
    (my_plugin.direct = 1) : (my_plugin.direct = 0);
}

void
INKConnectionFinish(void *req_id, INKConnectionStatus conn_status)
{
  if (conn_status == INK_TIME_EXPIRE)
    my_plugin.unfinished_requests++;
  free(req_id);
}

void
INKPluginFinish()
{
  int i;
  free(my_plugin.target_host);
  free(my_plugin.target_port);
  for (i = 0; i < my_plugin.num_origin_server; i++) {
    free(my_plugin.origin_host[i]);
    free(my_plugin.origin_port[i]);
  }
}

/* generate requests that are understood by the SDKtest_server
 *   URL Format:
 *     http://hostname:port/serial_number/lengthxxx
 *   Note:
 *     request_buf has to be filled in the complete request header
 *     i.e. GET URL HTTP/1.0 ....
 */
int
INKRequestCreate(char *origin_server_host /* return */ , int max_hostname_size,
                 char *origin_server_port /* return */ , int max_portname_size,
                 char *request_buf /* return */ , int max_request_size,
                 void **req_id /* return */ )
{
  User *user;
  int origin_server_num = (lrand48() % my_plugin.num_origin_server);
  long serial_number = generate_serial_number();
  long doc_size = generate_size();
  int pin = coin_toss(my_plugin.pin_ratio);
  int hostset = coin_toss(my_plugin.hostset_ratio);

  if (my_plugin.direct) {
    sprintf(origin_server_host, my_plugin.origin_host[origin_server_num]);
    sprintf(origin_server_port, my_plugin.origin_port[origin_server_num]);
    if (pin) {
      my_plugin.pin_requests++;

      if (hostset) {
        my_plugin.hostset_requests++;
        sprintf(request_buf,
                "GET /%ld/length%ld HTTP/1.0\r\nAccept: */*\r\nHost: %s:%s\r\nCacheTester-Pin: %d\r\nCacheTester-HostNameSet: 1\r\n\r\n",
                serial_number, doc_size, origin_server_host, origin_server_port, lrand48() % 3600);
      } else {
        sprintf(request_buf,
                "GET /%ld/length%ld HTTP/1.0\r\nAccept: */*\r\nHost: %s:%s\r\nCacheTester-Pin: %d\r\n\r\n",
                serial_number, doc_size, origin_server_host, origin_server_port, lrand48() % 3600);
      }
    } else {
      if (hostset) {
        my_plugin.hostset_requests++;
        sprintf(request_buf,
                "GET /%ld/length%ld HTTP/1.1\r\nAccept: */*\r\nHost: %s:%s\r\nCacheTester-HostNameSet: 1\r\n\r\n",
                serial_number, doc_size, origin_server_host, origin_server_port);
      } else {
        sprintf(request_buf,
                "GET /%ld/length%ld HTTP/1.1\r\nAccept: */*\r\nHost: %s:%s\r\n\r\n",
                serial_number, doc_size, origin_server_host, origin_server_port);
      }
    }
  } else {
    sprintf(origin_server_host, my_plugin.target_host);
    sprintf(origin_server_port, my_plugin.target_port);
    if (pin) {
      my_plugin.pin_requests++;

      if (hostset) {
        my_plugin.hostset_requests++;
        sprintf(request_buf,
                "GET http://%s:%s/%ld/length%ld HTTP/1.0\r\nAccept: */*\r\nCacheTester-Pin: %d\r\nCacheTester-HostNameSet: 1\r\n\r\n",
                my_plugin.origin_host[origin_server_num],
                my_plugin.origin_port[origin_server_num], serial_number, doc_size, lrand48() % 3600);
      } else {
        sprintf(request_buf,
                "GET http://%s:%s/%ld/length%ld HTTP/1.0\r\nAccept: */*\r\nCacheTester-Pin: %d\r\n\r\n",
                my_plugin.origin_host[origin_server_num],
                my_plugin.origin_port[origin_server_num], serial_number, doc_size, lrand48() % 3600);
      }
    } else {
      if (hostset) {
        my_plugin.hostset_requests++;
        sprintf(request_buf,
                "GET http://%s:%s/%ld/length%ld HTTP/1.1\r\nAccept: */*\r\nCacheTester-HostNameSet: 1\r\n\r\n",
                my_plugin.origin_host[origin_server_num],
                my_plugin.origin_port[origin_server_num], serial_number, doc_size);
      } else {
        sprintf(request_buf,
                "GET http://%s:%s/%ld/length%ld HTTP/1.1\r\nAccept: */*\r\n\r\n",
                my_plugin.origin_host[origin_server_num],
                my_plugin.origin_port[origin_server_num], serial_number, doc_size);
      }
    }
  }
  user = (User *) malloc(sizeof(User));
  user->doc_size_requested = doc_size;
  *req_id = (void *) user;
  my_plugin.requests++;
  return TRUE;
}

/* process response header returned either from synthetic
 * server or from proxy server
 */
INKRequestAction
INKHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  ((User *) req_id)->header_bytes = length;
  return INK_KEEP_GOING;
}

/* process part of the response returned either from synthetic
 * server or from proxy server
 */
INKRequestAction
INKPartialBodyProcess(void *req_id, void *partial_content, int partial_length, int accum_length)
{
  if (partial_length == 0) {
    if (accum_length >= ((User *) req_id)->doc_size_requested) {
      my_plugin.total_bytes_received += (accum_length + ((User *) req_id)->header_bytes);
      my_plugin.successful_requests++;
    } else {
      fprintf(stderr, "ERROR: received bytes < requested bytes");
    }
  }
  return INK_KEEP_GOING;
}


/* output report data after the SDKtest report */
void
INKReport()
{
  INKReportSingleData("Total Requests", "count", INK_SUM, (double) my_plugin.requests);
  INKReportSingleData("Successful Documents", "count", INK_SUM, (double) my_plugin.successful_requests);
  INKReportSingleData("Unfinished Documents", "count", INK_SUM, (double) my_plugin.unfinished_requests);
  INKReportSingleData("Total Bytes Received", "count", INK_SUM, (double) my_plugin.total_bytes_received);
}

/******************** ADDED FUNCTIONS ****************************/

/* determine if we are generating a document from a hotset of not */
long
generate_serial_number()
{
  long serial_number;
  double rand = drand48();
  /* Generate a document from hotset */
  if (rand < my_plugin.hotset_access_ratio) {
    serial_number = lrand48() % my_plugin.max_hotset_serial_num;
  } else {
    serial_number = my_plugin.max_hotset_serial_num + lrand48() %
      (my_plugin.max_docset_serial_num - my_plugin.max_hotset_serial_num);
  }
  return serial_number;
}

/* generate the size requesting for a request */
long
generate_size()
{
  int i;
  double rand = drand48();
  for (i = 0; i<my_plugin.num_sizes && rand> my_plugin.cumulative_size_prob[i]; i++);
  if (i == my_plugin.num_sizes) {
    fprintf(stderr, "Error: drand48() generated greater than 1.0 %f in generate_size_str\n", rand);
    exit(1);
  }
  return my_plugin.sizes[i];
}

/* read in the document size distribution file */
void
read_docsize_dist()
{
  int end_of_file = 0;
  int n;
  long size;
  double prob, avg_doc_size = 0.0;
  my_plugin.num_sizes = 0;
  do {
    n = fscanf(my_plugin.docsize_dist_file_p, "%ld %lf", &size, &prob);
    /* fscanf will return number of matched items. If it
     * returns EOF that means none were matched */
    if (n == EOF) {
      end_of_file = 1;
    } else if (n == 2) {
      my_plugin.sizes[my_plugin.num_sizes] = size;
      if (my_plugin.num_sizes == 0) {
        my_plugin.cumulative_size_prob[my_plugin.num_sizes] = prob;
      } else {
        my_plugin.cumulative_size_prob[my_plugin.num_sizes] =
          my_plugin.cumulative_size_prob[my_plugin.num_sizes - 1] + prob;
      }
      my_plugin.num_sizes++;
    } else {
      fprintf(stderr, "Error in docsize_dist_file\n");
      exit(1);
    }
  } while (!end_of_file);
  printf("Average Doc Size according to the specified distribution: %.2f\n", avg_doc_size);
  if ((my_plugin.cumulative_size_prob[my_plugin.num_sizes - 1] < 0.99999) ||
      (my_plugin.cumulative_size_prob[my_plugin.num_sizes - 1] > 1.00001)) {
    fprintf(stderr, "Error in docsize_dist_file: prob add up to %f\n",
            my_plugin.cumulative_size_prob[my_plugin.num_sizes - 1]);
    exit(1);
  }
  if (fclose(my_plugin.docsize_dist_file_p)) {
    fprintf(stderr, "Error: could not close the docsize_dist file\n");
    perror("Error: Docsize File Close");
    exit(1);
  }
}

/* parse the origin_server string in config file.
 * value is in the form of "host1:port1 host2:port2 ..."
 */
void
parse_origin_server_str(char *value)
{
  int i = 0;
  int len = strlen(value);
  char *value_cp = strdup(value);
  char *host_str, *port_str, *serv_str;

  my_plugin.num_origin_server = 0;
  while (i < len && my_plugin.num_origin_server < MAX_ORIGIN_SERVERS) {
    /* skip spaces in between */
    while (i < len && isspace(value_cp[i]))
      i++;
    if (i >= len)
      break;

    host_str = value_cp + i;
    port_str = strchr(host_str, ':') + 1;
    serv_str = strchr(port_str, ' ');
    *(port_str - 1) = '\0';
    if (serv_str) {
      *(serv_str) = '\0';
    } else {
      serv_str = value_cp + len;
    }
    my_plugin.origin_host[my_plugin.num_origin_server] = strdup(host_str);
    my_plugin.origin_port[my_plugin.num_origin_server] = strdup(port_str);
    i += (serv_str - host_str + 1);
    my_plugin.num_origin_server++;
  }
  assert(my_plugin.num_origin_server < MAX_ORIGIN_SERVERS);
  free(value_cp);
}

int
coin_toss(double max_value)
{
  double rand;
  rand = drand48();
  if (rand < max_value) {
    return TRUE;
  } else {
    return FALSE;
  }
}
