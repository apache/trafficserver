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

/* SimSynthServerCache.c 
 *
 * 
 * Description:
 *   - Simulate the default way of responding requests by the SDKtest_server
 *     with cachability specified in the server response header in some
 *     ratio.
 *
 * Added Options in Synth_server.config -    
 *     cachability : percentage of cachable response we want to generate
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ServerAPI.h"

#define MAX_HEADER_RESPONSE 256
#define TRUE 1
#define FALSE 0

typedef struct
{
  int status_code;
  long request_length;
  long bytes_not_sent;
  char header_response[MAX_HEADER_RESPONSE];
  int done_sent_header;         /* flag to see if header has been sent or not */
} RequestInfo;

typedef struct
{
  double cachability_ratio;     /* for cachability_ratio */
} SCPlugin;

SCPlugin my_plugin;

/* generate a random number to see if the document is cachable or not */
int
generate_cachability()
{
  double rand;
  rand = drand48();
  if (rand < my_plugin.cachability_ratio) {
    return TRUE;
  } else {
    return FALSE;
  }
}


void
INKOptionProcess(char *option, char *value)
{
  if (strcmp(option, "cachability") == 0) {
    my_plugin.cachability_ratio = (double) (atoi(value)) / 100.0;
  }
}


void
INKPluginInit()
{
  fprintf(stderr, "*** SimSynthServerCache for Synthetic Server ***\n");
  INKFuncRegister(INK_FID_OPTIONS_PROCESS);
  INKFuncRegister(INK_FID_RESPONSE_PREPARE);
  INKFuncRegister(INK_FID_RESPONSE_PUT);
}

/* prepare response header for a request */
int
INKResponsePrepare(char *req_hdr, int req_len, void **response_id)
{
  char *len_string;
  RequestInfo *resp_id = (RequestInfo *) malloc(sizeof(RequestInfo));
  int cachable = generate_cachability();

  resp_id->done_sent_header = FALSE;
  if ((len_string = strstr(req_hdr, "length"))) {
    len_string += strlen("length");
    resp_id->request_length = atoi(len_string);
    resp_id->bytes_not_sent = resp_id->request_length;
    resp_id->status_code = 200;

    if (cachable) {
      sprintf(resp_id->header_response, "%s\r\n%s\r\n%s%ld\r\n\r\n",
              "HTTP/1.0 200 OK", "Content-type: text/plain", "Content-length: ", resp_id->request_length);
    } else {
      sprintf(resp_id->header_response, "%s\r\n%s\r\n%s\r\n%s%ld\r\n\r\n",
              "HTTP/1.0 200 OK",
              "Content-type: text/plain", "Cache-control: no-cache", "Content-length: ", resp_id->request_length);
    }
  } else {
    resp_id->request_length = -1;
    resp_id->status_code = 404;
    resp_id->bytes_not_sent = 0;
    sprintf(resp_id->header_response, "%s\r\n%s\r\n\r\n", "HTTP/1.0 404 Not Found", "Content-type: text/plain");
  }
  *response_id = resp_id;
  return TRUE;
}

/* put response (response header + response document) into buffer */
void
INKResponsePut(void **resp_id /* return */ ,
               void *resp_buffer /* return */ ,
               int *resp_bytes /* return */ ,
               int resp_buffer_size, int bytes_last_response)
{
  int i = 0;
  RequestInfo *rid = *((RequestInfo **) resp_id);

  /* copy the header into the response buffer */
  if (!rid->done_sent_header) {
    i = sprintf((char *) resp_buffer, "%s", rid->header_response);
    rid->done_sent_header = TRUE;
  }

  /* copy the content into the response buffer and
   * the response would be like "XXXXXX......E" 
   */
  if (rid->status_code == 200) {
    if (rid->bytes_not_sent + i > resp_buffer_size) {
      memset((void *) ((char *) resp_buffer + i), 'X', resp_buffer_size - i);
      *resp_bytes = resp_buffer_size;
      rid->bytes_not_sent -= (resp_buffer_size - i);
    } else {
      memset((void *) ((char *) resp_buffer + i), 'X', rid->bytes_not_sent);
      memset((void *) ((char *) resp_buffer + i + rid->bytes_not_sent - 1), 'E', 1);
      *resp_bytes = rid->bytes_not_sent + i;
      rid->bytes_not_sent = 0;

    }
  }
  /* return NULL as the resp_id to indicate 
   * if it is the last INKResponsePut call */
  if (rid->bytes_not_sent <= 0 || rid->status_code != 200) {
    free(rid);
    *((RequestInfo **) resp_id) = NULL;
  }
}
