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
 *   Simulate server response that contains:
 *      - PSI header
 *      - PSI include in body
 *
 *   Ratio for generating PSI response is specified in config file.
 *
 * Added Options in Synth_server.config -
 *     psi_ratio : percentage of response with psi embedded we want to generate
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ServerAPI.h"

#define PSI_TAG_FORMAT "<!--include=file%d.txt-->"
#define PSI_TAG_MAX_SIZE 128
#define PSI_MIME_HEADER "X-Psi: true"

#define MAX_HEADER_RESPONSE 256
#define TRUE 1
#define FALSE 0

typedef struct {
  int status_code;
  long request_length;
  long bytes_not_sent;
  char header_response[MAX_HEADER_RESPONSE];
  int done_sent_header; /* flag to see if header has been sent or not */
  int psi;
} RequestInfo;

typedef struct {
  double psi_ratio; /* for psi_ratio */
} SCPlugin;

SCPlugin my_plugin;

/* generate a random number to see if the document should include psi or not */
int
generate_psibility()
{
  double rand;
  rand = drand48();
  if (rand < my_plugin.psi_ratio) {
    return TRUE;
  } else {
    return FALSE;
  }
}

void
TSOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "psi_ratio") == 0) {
    fprintf(stderr, "psi ratio set to %d %%\n", atoi(value));
    my_plugin.psi_ratio = (double)(atoi(value)) / 100.0;
  }
}

void
TSPluginInit()
{
  fprintf(stderr, "*** PSI Server ***\n");
  TSFuncRegister(TS_FID_OPTIONS_PROCESS);
  TSFuncRegister(TS_FID_RESPONSE_PREPARE);
  TSFuncRegister(TS_FID_RESPONSE_PUT);
}

/* prepare response header for a request */
int
TSResponsePrepare(char *req_hdr, int req_len, void **response_id)
{
  char *len_string;
  RequestInfo *resp_id = (RequestInfo *)malloc(sizeof(RequestInfo));

  resp_id->psi = generate_psibility();

  resp_id->done_sent_header = FALSE;
  if ((len_string = strstr(req_hdr, "length"))) {
    len_string += strlen("length");
    resp_id->request_length = atoi(len_string);
    resp_id->bytes_not_sent = resp_id->request_length;
    resp_id->status_code    = 200;

    if (resp_id->psi) {
      sprintf(resp_id->header_response, "%s\r\n%s\r\n%s%ld\r\n%s\r\n\r\n", "HTTP/1.0 200 OK", "Content-type: text/plain",
              "Content-length: ", resp_id->request_length, PSI_MIME_HEADER);
    } else {
      sprintf(resp_id->header_response, "%s\r\n%s\r\n%s%ld\r\n\r\n", "HTTP/1.0 200 OK", "Content-type: text/plain",
              "Content-length: ", resp_id->request_length);
    }
  } else {
    resp_id->request_length = -1;
    resp_id->status_code    = 404;
    resp_id->bytes_not_sent = 0;
    sprintf(resp_id->header_response, "%s\r\n%s\r\n\r\n", "HTTP/1.0 404 Not Found", "Content-type: text/plain");
  }
  *response_id = resp_id;
  return TRUE;
}

/* put response (response header + response document) into buffer */
void
TSResponsePut(void **resp_id /* return */, void *resp_buffer /* return */, int *resp_bytes /* return */, int resp_buffer_size,
              int bytes_last_response)
{
  int i            = 0;
  RequestInfo *rid = *((RequestInfo **)resp_id);

  /* copy the header into the response buffer */
  if (!rid->done_sent_header) {
    i                     = sprintf((char *)resp_buffer, "%s", rid->header_response);
    rid->done_sent_header = TRUE;
  }

  /* copy the content into the response buffer      */
  /* for a psi response, it will look like:         */
  /*    XXX...XXX<!--include=fileN.txt-->XXX...XXXE   */
  /* with 0<= N <= 99                               */
  /*                                                */
  /* for non psi response, it will looke like:      */
  /*   XXX...XXXE                                    */

  if (rid->status_code == 200) {
    /* buffer is not large enough to handle all content */
    if (rid->bytes_not_sent + i > resp_buffer_size) {
      memset((void *)((char *)resp_buffer + i), 'X', resp_buffer_size - i);
      *resp_bytes = resp_buffer_size;
      rid->bytes_not_sent -= (resp_buffer_size - i);
    }
    /* buffer is large enough to handle it in one shot */
    else {
      if (rid->psi) {
        /* generate our psi tag: <!--include=fileN.txt--> */
        char psi_tag[PSI_TAG_MAX_SIZE];
        int len = sprintf(psi_tag, PSI_TAG_FORMAT, rand() % 100);

        /* hopefully enough space for our include command */
        if (rid->bytes_not_sent >= len) {
          memcpy((void *)((char *)resp_buffer + i), psi_tag, len);
          rid->bytes_not_sent -= len;
          i += len;
        }
      }
      memset((void *)((char *)resp_buffer + i), 'X', rid->bytes_not_sent);
      memset((void *)((char *)resp_buffer + i + rid->bytes_not_sent - 1), 'E', 1);
      *resp_bytes         = rid->bytes_not_sent + i;
      rid->bytes_not_sent = 0;
    }
  }
  /* return NULL as the resp_id to indicate
   * if it is the last TSResponsePut call */
  if (rid->bytes_not_sent <= 0 || rid->status_code != 200) {
    free(rid);
    *((RequestInfo **)resp_id) = NULL;
  }
}
