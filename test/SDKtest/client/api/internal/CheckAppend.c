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

/* CheckAppend.c
 *
 *
 * Description:
 *   - Checks the responses that are received from the proxy to see if they have
 *     the text appended as specified in the client configuration file.
 * Designed to test append-transform plugin under load.
 *
 * Use get_append_file script to get the append_file from a machine on which TS is
 * running.
 * Make sure that append-transform plugin is loaded on the TS-proxy used.
 *
 * Added Options in client/SDKtest_client.config -
 *   append-file-path : full path of the file containing the appended text
 *              eg. /home/user/file.txt
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include "ClientAPI.h"

#define MAX_LOG_STR_SIZE 256
#define MAX_PATH_SIZE 256
#define TRUE 1
#define FALSE 0
#define PRINT_DEBUG


#ifdef PRINT_DEBUG
#define print_debug(x) fprintf x
#else
#define print_debug(x) ;
#endif

typedef struct
{
  char append_file_path[MAX_PATH_SIZE];
  int append_file;
  struct stat statbuf;
  char *append_content;
  int append_len;
} CheckAppendPlugin;

typedef struct
{
  char *tail_of_resp;
  int tail_of_resp_index;
} CONN_DATA;

CheckAppendPlugin my_plugin;

void
TSPluginInit(int clientid)
{
  fprintf(stderr, "*** CheckAppend Test for append-transform-plugin v0.0***\n");
  TSFuncRegister(TS_FID_REQUEST_CREATE);
  TSFuncRegister(TS_FID_CONNECTION_FINISH);
  TSFuncRegister(TS_FID_OPTIONS_PROCESS);
  TSFuncRegister(TS_FID_OPTIONS_PROCESS_FINISH);
  TSFuncRegister(TS_FID_HEADER_PROCESS);
  TSFuncRegister(TS_FID_PARTIAL_BODY_PROCESS);
}

void
TSConnectionFinish(void *request_id, TSConnectionStatus status)
{

  /*print_debug((stderr, "Freeing %x\n", request_id)); */
  free(((CONN_DATA *) request_id)->tail_of_resp);
  free(request_id);

  /*fprintf(stderr, "CONN %x FINISH with status", request_id);
     switch(status) {
     case TS_CONN_COMPLETE:
     fprintf(stderr, "TS_CONN_COMPLETE\n");
     break;

     case TS_CONN_ERR:
     fprintf(stderr, "TS_CONN_ERR\n");
     break;

     case TS_READ_ERR:
     fprintf(stderr, "TS_READ_ERR\n");
     break;

     case TS_WRITE_ERR:
     fprintf(stderr, "TS_WRITE_ERR\n");
     break;

     case TS_TIME_EXPIRE:
     fprintf(stderr, "TS_TIME_EXPIRE\n");
     break;
     } */
}


int
TSRequestCreate(char *server_host, int max_host_size, char *server_port,
                 int max_port_size, char *request_buf, int max_request_size, void **request_id)
{

  CONN_DATA *conn_data;
  int i;

  conn_data = (CONN_DATA *) malloc(sizeof(CONN_DATA));

  if (conn_data == NULL) {
    fprintf(stderr, "ERROR!! NOT ENOUGH MEMORY for conn_data\n");
    return 0;
  }

  *request_id = (void *) conn_data;
  /*print_debug((stderr, "Allocated %x\n", *request_id)); */

  conn_data->tail_of_resp = (char *) malloc(sizeof(char) * (my_plugin.append_len + 1));
  if (conn_data->tail_of_resp == NULL) {
    fprintf(stderr, "Error: malloc error\n");
    return 0;
  }

  conn_data->tail_of_resp_index = 0;
  for (i = 0; i < my_plugin.append_len + 1; i++) {
    conn_data->tail_of_resp[i] = 'Q';
  }

  return 1;
}


void
TSOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "append-file-path") == 0) {
    if (strlen(value) < MAX_PATH_SIZE) {
      strcpy(my_plugin.append_file_path, value);
    } else {
      fprintf(stderr, "log_path size exceeds MAX_PATH_SIZE\n");
      exit(1);
    }
  }
}

void
TSOptionsProcessFinish()
{
  int n;

/* Open file containing appended contents */

  if ((my_plugin.append_file = open(my_plugin.append_file_path, O_RDONLY)) == -1) {
    fprintf(stderr, "Error: Unable to open %s\n", my_plugin.append_file_path);
    exit(1);
  }

/* Get file size in bytes */

  if (fstat(my_plugin.append_file, &my_plugin.statbuf) == -1) {
    fprintf(stderr, "Error: fstat error for %s\n", my_plugin.append_file_path);
    close(my_plugin.append_file);
    exit(1);
  }

  my_plugin.append_len = my_plugin.statbuf.st_size;

  my_plugin.append_content = (char *) malloc(sizeof(char) * (my_plugin.append_len + 1));
  if (my_plugin.append_content == NULL) {
    close(my_plugin.append_file);
    fprintf(stderr, "Error: malloc error\n");
    exit(1);
  }

/* Read append-file contents in append_content buffer */

  n = read(my_plugin.append_file, my_plugin.append_content, my_plugin.append_len);
  if (n == -1) {
    close(my_plugin.append_file);
    fprintf(stderr, "Error: read error for %s\n", my_plugin.append_file_path);
    exit(1);
  }

  if (n != my_plugin.append_len) {
    close(my_plugin.append_file);
    fprintf(stderr, "Error: Actual read bytes mismatch for %s\n", my_plugin.append_file_path);
    exit(1);
  }

  my_plugin.append_content[n] = '\0';

  close(my_plugin.append_file);

}

TSRequestAction
TSHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  return TS_KEEP_GOING;
}

TSRequestAction
TSPartialBodyProcess(void *request_id, void *partial_content, int partial_length, int accum_length)
{
  char *src;
  int room;
  char *last_byte;
  CONN_DATA *req_id;

  print_debug((stderr, "req_id %x accum_length %d", req_id, accum_length));
  print_debug((stderr, "Rx response %d bytes\n", partial_length));

  req_id = (CONN_DATA *) request_id;
  if (req_id == NULL)
    return TS_STOP_FAIL;

  if (partial_length == 0) {    /* Response is now complete. */

    if (memcmp(req_id->tail_of_resp, my_plugin.append_content, my_plugin.append_len) == 0) {

      return TS_STOP_SUCCESS;
    }

    fprintf(stderr, "Test Failed: appended content doesn't match\n");
    fprintf(stderr, "append: [%s] tail_of_resp [%s]\n", my_plugin.append_content, req_id->tail_of_resp);
    return TS_STOP_FAIL;
  }

/* Response not complete. Copy last _useful_ bytes to tail_of_file buffer */

  last_byte = (char *) partial_content + partial_length;

  if (partial_length > my_plugin.append_len)
    partial_length = my_plugin.append_len;

  src = last_byte - partial_length;

/* append_at_end(src, partial_length); */

  room = my_plugin.append_len - req_id->tail_of_resp_index;
  if (room < partial_length) {
    memmove(req_id->tail_of_resp, req_id->tail_of_resp + partial_length - room, partial_length - room);
    req_id->tail_of_resp_index -= partial_length - room;
  }

  memcpy(req_id->tail_of_resp + req_id->tail_of_resp_index, src, partial_length);

  req_id->tail_of_resp_index += partial_length;
  req_id->tail_of_resp[req_id->tail_of_resp_index + 1] = '\0';

  return TS_KEEP_GOING;

}
