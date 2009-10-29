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

/* SingleHostFilter.c
 *
 *
 *   Added Options in SDKtest_client.config -
 *
 *     forbidden_ratio : percentage of forbidden requests we want to
 *                       generate		       
 *
 *     forbidden_host  : the blacklisted host name 
 *                       eg. www.playboy.com
 */

#include "ClientAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TRUE 1
#define FALSE 0
#define MAX_URL_SIZE 256
typedef enum
{
  ALLOWED,
  FORBIDDEN
} URL_TYPE;

typedef struct
{
  /* for determining if the proxy is in active or not */
  int direct;
  char *target_host;
  char *target_port;

  /* for filtering test */
  double forbidden_ratio;
  char *forbidden_host;

  /* for reporting */
  long requests;
  long allowed_requests;
  long forbidden_requests;

} FilterPlugin;

/* helper functions */
URL_TYPE select_url_catagory();

FilterPlugin my_plugin;

void
INKPluginInit(int client_id)
{
  my_plugin.requests = 0;
  my_plugin.allowed_requests = 0;
  my_plugin.forbidden_requests = 0;
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
  } else if (strcmp(option, "forbidden_host") == 0) {
    my_plugin.forbidden_host = strdup(value);
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
INKPluginFinish()
{
  /* do clean up here */
  free(my_plugin.target_host);
  free(my_plugin.target_port);
  free(my_plugin.forbidden_host);
}


int
INKRequestCreate(char *origin_server_host /* return */ , int max_hostname_size,
                 char *origin_server_port /* return */ , int max_portname_size,
                 char *request_buf /* return */ , int max_request_size,
                 void **req_id /* return */ )
{
  char *portname = (char *) malloc(max_portname_size + 1);
  char *hostname = (char *) malloc(max_hostname_size + 1);
  URL_TYPE type = select_url_catagory();

  if (type == FORBIDDEN) {
    strcpy(hostname, my_plugin.forbidden_host);
    my_plugin.forbidden_requests++;

    if (my_plugin.direct) {
      strcpy(origin_server_host, hostname);
      strcpy(origin_server_port, portname);
      sprintf(request_buf,
              "GET %s HTTP/1.0\r\nAccept: */*\r\nHost: %s:%s\r\n\r\n",
              "/index.html", origin_server_host, origin_server_port);
    } else {
      strcpy(origin_server_host, my_plugin.target_host);
      strcpy(origin_server_port, my_plugin.target_port);
      sprintf(request_buf, "GET %s/%s HTTP/1.0\r\nAccept: */*\r\n\r\n", hostname, "index.html");
    }
  } else {
    /* use the default way of generating request from SDKtest_client */
    strcpy(origin_server_host, "");
    strcpy(origin_server_port, "");
    strcpy(request_buf, "");
    my_plugin.allowed_requests++;
  }

  my_plugin.requests++;
  return TRUE;
}


INKRequestAction
INKHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  if (strstr(header, "200 OK")) {
    return INK_KEEP_GOING;
  }
  /* since SDKtest_client core will treat non 200 response as fail request,
   * we need to specify it as a successful request explicitly
   */
  else if (strstr(header, "403 Forbidden")) {
    return INK_STOP_SUCCESS;
  } else if (strstr(header, "302 Moved Temporarily")) {
    return INK_STOP_SUCCESS;
  } else {
    return INK_STOP_FAIL;
  }
}


#define safediv(top,bottom) ((bottom) ? (((double)(top))/((double)(bottom))) : (bottom))
void
INKReport()
{
  INKReportSingleData("Total Requests", "count", INK_SUM, (double) my_plugin.requests);
  INKReportSingleData((char *) "Allowed Requests", (char *) "count", INK_SUM, (double) my_plugin.allowed_requests);
  INKReportSingleData((char *) "Forbidden Requests", (char *) "count", INK_SUM, (double) my_plugin.forbidden_requests);
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
