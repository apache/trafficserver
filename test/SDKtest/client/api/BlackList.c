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

/* BlackList.c
 *
 *
 * Description
 *   - Generate requests to the web servers listed in the files specified
 *     by allowed_host_file and forbidden_host_file in some forbidden
 *     ratio specified in the configuration file.  This example is
 *     targeted to test the performance of proxy server with filtering
 *     functionality.
 *
 * Added Options in SDKtest_client.config -
 *   forbidden_ratio     : percentage of blacklisted requests we
 *                         want to generate
 *   allowed_host_file   : full path of the file that contains the
 *                         allowed sites
 *   forbidden_host_file : full path of the file that contains the
 *                         forbidden sites
 */

#include "ClientAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TRUE 1
#define FALSE 0
#define MAX_URL_SIZE 256
typedef enum
{
  ALLOWED,
  FORBIDDEN
} URL_TYPE;

/* helper functions */
void read_host(FILE * url, char *buffer, int buf_size);
URL_TYPE select_url_catagory();

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

  /* for filtering test */
  FILE *allowed_host_p;
  FILE *forbidden_host_p;
  double forbidden_ratio;

  /* for reporting data */
  long requests;
  long allowed_requests;
  long forbidden_requests;

  long successful_documents;
  long forbidden_documents;
  long redirect_documents;
  long unfinished_documents;
  long other_failed_documents;
  long total_bytes_received;
} BlackListPlugin;

BlackListPlugin my_plugin;

void
INKPluginInit(int client_id)
{
  my_plugin.requests = 0;
  my_plugin.allowed_requests = 0;
  my_plugin.forbidden_requests = 0;
  my_plugin.successful_documents = 0;
  my_plugin.forbidden_documents = 0;
  my_plugin.redirect_documents = 0;
  my_plugin.unfinished_documents = 0;
  my_plugin.other_failed_documents = 0;
  my_plugin.total_bytes_received = 0;

  /* setup the callbacks */
  INKFuncRegister(INK_FID_OPTIONS_PROCESS);
  INKFuncRegister(INK_FID_OPTIONS_PROCESS_FINISH);
  INKFuncRegister(INK_FID_CONNECTION_FINISH);
  INKFuncRegister(INK_FID_PLUGIN_FINISH);
  INKFuncRegister(INK_FID_REQUEST_CREATE);
  INKFuncRegister(INK_FID_HEADER_PROCESS);
  INKFuncRegister(INK_FID_PARTIAL_BODY_PROCESS);
  INKFuncRegister(INK_FID_REPORT);
}


void
INKOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "target_host") == 0) {
    my_plugin.target_host = strdup(value);
  } else if (strcmp(option, "target_port") == 0) {
    my_plugin.target_port = strdup(value);
  } else if (strcmp(option, "forbidden_ratio") == 0) {
    my_plugin.forbidden_ratio = (double) (atoi(value)) / 100.0;
  } else if (strcmp(option, "allowed_host_file") == 0) {
    if (!(my_plugin.allowed_host_p = fopen(value, "r"))) {
      fprintf(stderr, "Open URL file %s failed\n", value);
      exit(1);
    }
  } else if (strcmp(option, "forbidden_host_file") == 0) {
    if (!(my_plugin.forbidden_host_p = fopen(value, "r"))) {
      fprintf(stderr, "Open URL file %s failed\n", value);
      exit(1);
    }
  }
}


void
INKOptionsProcessFinish()
{
  if ((strlen(my_plugin.target_host) == 0) || (strlen(my_plugin.target_port) == 0)) {
    my_plugin.direct = 1;
  } else {
    my_plugin.direct = 0;
  }
}

void
INKConnectionFinish(void *req_id, INKConnectionStatus conn_status)
{
  if (conn_status == INK_TIME_EXPIRE) {
    my_plugin.unfinished_documents++;
  }
  free(req_id);
}


void
INKPluginFinish()
{
  /* do all cleanup here */
  free(my_plugin.target_host);
  free(my_plugin.target_port);
  fclose(my_plugin.allowed_host_p);
  fclose(my_plugin.forbidden_host_p);
}


int
INKRequestCreate(char *origin_server_host /* return */ , int max_hostname_size,
                 char *origin_server_port /* return */ , int max_portname_size,
                 char *request_buf /* return */ , int max_request_size,
                 void **req_id /* return */ )
{
  char *portname = "80";
  char *hostname = (char *) malloc(max_hostname_size + 1);
  URL_TYPE type = select_url_catagory();

  if (type == FORBIDDEN) {
    read_host(my_plugin.forbidden_host_p, hostname, max_hostname_size);
    my_plugin.forbidden_requests++;
  } else {
    read_host(my_plugin.allowed_host_p, hostname, max_hostname_size);
    my_plugin.allowed_requests++;
  }

  if (my_plugin.direct) {
    strcpy(origin_server_host, hostname);
    strcpy(origin_server_port, portname);
    sprintf(request_buf,
            "GET %s HTTP/1.0\r\nAccept: */*\r\nHost: %s:%s\r\n\r\n",
            "/index.html", origin_server_host, origin_server_port);
  } else {
    strcpy(origin_server_host, my_plugin.target_host);
    strcpy(origin_server_port, my_plugin.target_port);
    sprintf(request_buf, "GET %s:%s/%s HTTP/1.0\r\nAccept: */*\r\n\r\n", hostname, portname, "index.html");
  }
  *req_id = malloc(sizeof(User));
  my_plugin.requests++;
  return TRUE;
}


INKRequestAction
INKHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  ((User *) req_id)->header_bytes = length;
  if (strstr(header, "200 OK")) {
    return INK_KEEP_GOING;
  }

  /* we consider the request to the blacklisted
   * sites to be successful documents */
  else if (strstr(header, "403 Forbidden")) {
    my_plugin.forbidden_documents++;
    my_plugin.total_bytes_received += length;
    return INK_STOP_SUCCESS;
  } else if (strstr(header, "302 Moved Temprarily")) {
    my_plugin.redirect_documents++;
    my_plugin.total_bytes_received += length;
    return INK_STOP_SUCCESS;
  } else {
    my_plugin.other_failed_documents++;
    return INK_STOP_FAIL;
  }
}


INKRequestAction
INKPartialBodyProcess(void *req_id, void *partial_content, int partial_length, int accum_length)
{
  if (partial_length == 0) {
    my_plugin.successful_documents++;
    my_plugin.total_bytes_received += (accum_length + ((User *) req_id)->header_bytes);
  }
  return INK_KEEP_GOING;
}


void
INKReport()
{

  INKReportSingleData("Total Requests", "count", INK_SUM, (double) my_plugin.requests);
  INKReportSingleData("Allowed Requests", "count", INK_SUM, (double) my_plugin.allowed_requests);
  INKReportSingleData("Forbidden Requests", "count", INK_SUM, (double) my_plugin.forbidden_requests);
  INKReportSingleData("Successful Documents", "count", INK_SUM, (double) my_plugin.successful_documents);
  INKReportSingleData("Forbidden Documents", "count", INK_SUM, (double) my_plugin.forbidden_documents);
  INKReportSingleData("Redirect Documents", "count", INK_SUM, (double) my_plugin.redirect_documents);
  INKReportSingleData("Unfinished Documents", "count", INK_SUM, (double) my_plugin.unfinished_documents);
  INKReportSingleData("Other Fail Documents", "count", INK_SUM, (double) my_plugin.other_failed_documents);
  INKReportSingleData("Total Bytes Received", "count", INK_SUM, (double) my_plugin.total_bytes_received);
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
 *   based on the forbidden rate specified in SDKtest_client.config
 */
URL_TYPE
select_url_catagory()
{
  double rand;
  rand = drand48();
  if (rand < my_plugin.forbidden_ratio) {
    return FORBIDDEN;
  } else {
    return ALLOWED;
  }
}
