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

/* CheckContentServer.c
 *
 *
 * Description:
 *   - Response the client requests in the format of '0123456789012..'
 *     until it reaches the number of bytes the client requested.
 *     In that case, the client can check the correctness of the responsed
 *     document.
 *     Note: This example needs to work with CheckContentClient plugin
 *           for SDKtest_client.
 */

#include "ServerAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TRUE 1
#define FALSE 0
#define MAX_HEADER_RESPONSE 256

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
  char content[11];             /* a buffer contains "0123456789\0" */
} CheckContentPlugin;

CheckContentPlugin my_plugin;



void
INKPluginInit()
{
  int i = 0;

  fprintf(stderr, "*** CheckContentServer Test for Synthetic Server ***\n");
  fprintf(stderr, "*** needs to work with CheckContentClient plugin ***\n");

  /* initial my_plugin.content */
  while (i < 10) {
    my_plugin.content[i++] = i + '0';
  }
  my_plugin.content[i] = '\0';

  /* register functions to be callbacked */
  INKFuncRegister(INK_FID_RESPONSE_PREPARE);
  INKFuncRegister(INK_FID_RESPONSE_PUT);
}



int
INKResponsePrepare(char *req_hdr, int req_len, void **response_id)
{
  char *len_string;
  RequestInfo *resp_id = (RequestInfo *) malloc(sizeof(RequestInfo));

  resp_id->done_sent_header = FALSE;
  if ((len_string = strstr(req_hdr, "length"))) {
    len_string += strlen("length");
    resp_id->request_length = atoi(len_string);
    resp_id->bytes_not_sent = resp_id->request_length;
    resp_id->status_code = 200;
    sprintf(resp_id->header_response, "%s\r\n%s\r\n%s%ld\r\n\r\n",
            "HTTP/1.0 200 OK", "Content-type: text/plain", "Content-length: ", resp_id->request_length);
  } else {
    resp_id->request_length = -1;
    resp_id->status_code = 404;
    resp_id->bytes_not_sent = 0;
    sprintf(resp_id->header_response, "%s\r\n%s\r\n\r\n", "HTTP/1.0 404 Not Found", "Content-type: text/plain");
  }
  *response_id = resp_id;
  return TRUE;
}



void
INKResponsePut(void **resp_id /* return */ ,
               void *resp_buffer /* return */ ,
               int *resp_bytes /* return */ ,
               int resp_buffer_size, int bytes_last_response)
{
  int i = 0, copy;
  RequestInfo *rid = *((RequestInfo **) resp_id);

  *resp_bytes = 0;
  /* copy the header into the response buffer */
  if (!rid->done_sent_header) {
    i = sprintf((char *) resp_buffer, "%s", rid->header_response);
    rid->done_sent_header = TRUE;
    *resp_bytes += i;
  }

  /* copy the content into the response buffer */
  if (rid->status_code == 200) {
    while (rid->bytes_not_sent > 0 && i < resp_buffer_size) {
      /* response would be like "0123456789012..." */
      if (rid->bytes_not_sent >= 10) {
        copy = 10;
      } else {
        copy = rid->bytes_not_sent;
      }
      memcpy((char *) resp_buffer + i, my_plugin.content, copy);
      i += copy;
      rid->bytes_not_sent -= copy;
      *resp_bytes += copy;
    }
  }
  /* return NULL as the resp_id to indicate
   * that it is the last INKResponsePut call */
  if (rid->bytes_not_sent <= 0 || rid->status_code != 200) {
    free(rid);
    *((RequestInfo **) resp_id) = NULL;
  }
}
