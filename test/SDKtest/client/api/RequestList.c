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

/* RequestList.c
 *
 *
 * Description
 *   - Generate requests to the web servers listed in the files specified
 *     in the request lists, specified in the configuration file.
 *     Also use the ratio specified with the request lists to generate the
 *     right distribution of requests.
 *
 *
 * Added Options in SDKtest_client.config -
 *   request_lists : full path of the file[s] that contain the
 *                   request lists. Also need to specify the
 *                   request ratio. Example :
 *                   request_lists=/home/bob/list1:20,/home/bob/list2:80
 *                   Note: comma is the seperator. do not leave space or tabs inbetween
 *                   This will cause 20 % of requests to go from list1 and
 *                   80 % from list2.
 *                   Note : the ratios MUST add upto 100.
 */

#include "ClientAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TRUE 1
#define FALSE 0
#define MAX_URL_SIZE 256
#define MAX_LISTS 10
#define MAX_FILE_NAME_SIZE 512
#define MAX_SMALL_NAME 10

typedef enum
{
  ALLOWED,
  FORBIDDEN
} URL_TYPE;

/* helper functions */
void read_host(FILE * url, char *buffer, int buf_size);
int select_url_catagory();

typedef struct
{
  int header_bytes;
} User;

typedef struct
{
  /* for determining if we submit request to TS or not */
  int direct;
  char *target_host;
  char *target_port;

  /* for request lists */
  FILE *list_fp[MAX_LISTS];
  char *list_str[MAX_LISTS];
  double list_ratio[MAX_LISTS];
  int list_requests[MAX_LISTS];
  int nlist;

  /* for reporting data */
  long requests;

  long successful_documents;
  long unfinished_documents;
  long other_failed_documents;
  long total_bytes_received;

} RequestListPlugin;

RequestListPlugin my_plugin;

void
TSPluginInit(int client_id)
{
  my_plugin.requests = 0;
  my_plugin.successful_documents = 0;
  my_plugin.unfinished_documents = 0;
  my_plugin.other_failed_documents = 0;
  my_plugin.total_bytes_received = 0;

  /* setup the callbacks */
  TSFuncRegister(TS_FID_OPTIONS_PROCESS);
  TSFuncRegister(TS_FID_OPTIONS_PROCESS_FINISH);
  TSFuncRegister(TS_FID_CONNECTION_FINISH);
  TSFuncRegister(TS_FID_PLUGIN_FINISH);
  TSFuncRegister(TS_FID_REQUEST_CREATE);
  TSFuncRegister(TS_FID_HEADER_PROCESS);
  TSFuncRegister(TS_FID_PARTIAL_BODY_PROCESS);
  TSFuncRegister(TS_FID_REPORT);
}


void
TSOptionsProcess(char *option, char *value)
{
  int i;
  int rsum;

  if (strcmp(option, "target_host") == 0) {
    my_plugin.target_host = strdup(value);
  } else if (strcmp(option, "target_port") == 0) {
    my_plugin.target_port = strdup(value);
  } else if (strcmp(option, "request_lists") == 0) {

    char my_value[2046];
    strcpy(my_value, value);

    my_plugin.nlist = 0;
    my_plugin.list_str[my_plugin.nlist] = strtok(my_value, ",");

    while (my_plugin.list_str[my_plugin.nlist] != NULL) {
      my_plugin.nlist++;
      my_plugin.list_str[my_plugin.nlist] = strtok(NULL, ",");
    }

    rsum = 0;
    for (i = 0; i < my_plugin.nlist; i++) {
      char tmp_s[MAX_FILE_NAME_SIZE];
      char *fname;
      char *tmp;

      my_plugin.list_requests[i] = 0;
      strcpy(tmp_s, my_plugin.list_str[i]);

      fname = strtok(tmp_s, ":");
      tmp = strtok(NULL, ":");
      my_plugin.list_ratio[i] = (double) (atoi(tmp));
      rsum += my_plugin.list_ratio[i];

      if (!(my_plugin.list_fp[i] = fopen(fname, "r"))) {
        fprintf(stderr, "Open URL file %s failed\n", fname);
        exit(1);
      }
    }

    if (rsum != 100) {
      fprintf(stderr, "Sum of ratios [%d] != 100", rsum);
      exit(1);
    }

  }
}


void
TSOptionsProcessFinish()
{
  if ((strlen(my_plugin.target_host) == 0) || (strlen(my_plugin.target_port) == 0)) {
    my_plugin.direct = 1;
  } else {
    my_plugin.direct = 0;
  }
}

void
TSConnectionFinish(void *req_id, TSConnectionStatus conn_status)
{
  if (conn_status == TS_TIME_EXPIRE) {
    my_plugin.unfinished_documents++;
  }
  free(req_id);
}


void
TSPluginFinish()
{
  /* do all cleanup here */
  int i;
  free(my_plugin.target_host);
  free(my_plugin.target_port);

  for (i = 0; i < my_plugin.nlist; i++) {
    fclose(my_plugin.list_fp[i]);
  }
}


int
TSRequestCreate(char *origin_server_host /* return */ , int max_hostname_size,
                 char *origin_server_port /* return */ , int max_portname_size,
                 char *request_buf /* return */ , int max_request_size,
                 void **req_id /* return */ )
{
  /* char *portname = "80"; */
  char *portname;
  char *hostname = (char *) malloc(max_hostname_size + 1);
  char *tail = (char *) malloc(max_hostname_size + 1);

  int type = select_url_catagory();

  strcpy(tail, "index.html");
  if (type != -1) {

    char *tmp_h1 = (char *) malloc(max_hostname_size + 1);
    char *tmp_h2 = (char *) malloc(max_hostname_size + 1);

    my_plugin.list_requests[type]++;
    read_host(my_plugin.list_fp[type], hostname, max_hostname_size);

    strcpy(tmp_h1, hostname);
    tmp_h2 = strchr(tmp_h1, ':');

    if (tmp_h2 != NULL) {
      int lport, ltail;
      char *tmp_h3 = (char *) malloc(max_hostname_size + 1);

      lport = tmp_h2 - tmp_h1;
      *(hostname + lport) = '\0';
      portname = hostname + lport + 1;

      tmp_h3 = strchr(tmp_h1, '/');

      if (tmp_h3 != NULL) {
        ltail = tmp_h3 - tmp_h1;
        *(hostname + ltail) = '\0';
        tail = hostname + ltail + 1;
      }

    } else {
      portname = strdup("80");
    }
  } else {
    fprintf(stderr, "ERROR: unable to select url list; select_url_catagory returned -1\n");
    exit(1);
  }

  if (my_plugin.direct) {
    strcpy(origin_server_host, hostname);
    strcpy(origin_server_port, portname);
    sprintf(request_buf,
            "GET /%s HTTP/1.0\r\nAccept: */*\r\nHost: %s:%s\r\n\r\n", tail, origin_server_host, origin_server_port);
  } else {
    strcpy(origin_server_host, my_plugin.target_host);
    strcpy(origin_server_port, my_plugin.target_port);
    sprintf(request_buf, "GET %s:%s/%s HTTP/1.0\r\nAccept: */*\r\n\r\n", hostname, portname, tail);
  }
  *req_id = malloc(sizeof(User));
  my_plugin.requests++;
  return TRUE;
}


TSRequestAction
TSHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  ((User *) req_id)->header_bytes = length;

  if (strstr(header, "200 OK")) {
    return TS_KEEP_GOING;
  } else {
    my_plugin.other_failed_documents++;
    return TS_STOP_FAIL;
  }
}


TSRequestAction
TSPartialBodyProcess(void *req_id, void *partial_content, int partial_length, int accum_length)
{
  if (partial_length == 0) {
    my_plugin.successful_documents++;
    my_plugin.total_bytes_received += (accum_length + ((User *) req_id)->header_bytes);
  }
  return TS_KEEP_GOING;
}


void
TSReport()
{

  int i;

  TSReportSingleData("Total Requests", "count", TS_SUM, (double) my_plugin.requests);
  TSReportSingleData("Successful Documents", "count", TS_SUM, (double) my_plugin.successful_documents);
  TSReportSingleData("Unfinished Documents", "count", TS_SUM, (double) my_plugin.unfinished_documents);
  TSReportSingleData("Other Failed Documents", "count", TS_SUM, (double) my_plugin.other_failed_documents);

  for (i = 0; i < my_plugin.nlist; i++) {
    char s[MAX_FILE_NAME_SIZE + 30];
    sprintf(s, "Total Requests from file %d", i);
    TSReportSingleData(s, "count", TS_SUM, (double) my_plugin.list_requests[i]);
  }

  TSReportSingleData("Total Bytes Received", "count", TS_SUM, (double) my_plugin.total_bytes_received);
}


/******************** ADDED FUNCTIONS ****************************/

/*   Reads a host from the file pointer, url, and stores the host
 *   it read into the buffer.  If no more host is read, it starts
 *   to read the host name from the beginning of the file
 */
void
read_host(FILE * url, char *buffer, int buf_size)
{
  int c, i;
  while (1) {
    i = 0;
    while ((c = fgetc(url)) != EOF) {
      if (i < buf_size) {
        if (c == '\n' && strcmp(buffer, "")) {
          buffer[i] = '\0';
          return;
        }
        if (!isspace(c))
          buffer[i++] = c;
      }
      /* skip those host names that are longer than buf_size */
      else {
        while (((c = fgetc(url)) != EOF) && c != '\n');
        /* if hits the end, go back to the front */
        if (c == EOF) {
          if (fseek(url, 0, SEEK_SET)) {
            fprintf(stderr, "ERROR in fseek");
          }
        }
      }
    }
    /* if hits the end, go back to the front */
    if (c == EOF) {
      if (fseek(url, 0, SEEK_SET)) {
        fprintf(stderr, "ERROR in fseek");
      }
    }
  }
}


/*   Randomly choose what catagory of url we should generate
 *   based on the ratio for the file specified in SDKtest_client.config
 */
int
select_url_catagory()
{

  double rand;
  double x;
  int i;

  rand = drand48();

  x = 0;
  for (i = 0; i < my_plugin.nlist; i++) {
    x = x + (double) my_plugin.list_ratio[i] / 100;
    if (rand <= x) {
      return i;
    }
  }
  return -1;
}
