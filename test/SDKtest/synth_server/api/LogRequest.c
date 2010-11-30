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

/* LogRequest.c
 *
 *
 * Description:
 *   - Log the requests that are received from the clients into a file
 *     specified in the server configuration file.
 *
 * Added Options in Synth_server.config -
 *   log_path : full path of the log file
 *              eg. /home/user/file.log
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "ServerAPI.h"

#define MAX_LOG_STR_SIZE 256
#define MAX_PATH_SIZE 256
#define TRUE 1
#define FALSE 0

typedef struct
{
  /* for logging the request */
  char log_path[MAX_PATH_SIZE];
  FILE *request_log;
} LogRequestPlugin;

LogRequestPlugin my_plugin;

void
TSPluginInit()
{
  fprintf(stderr, "*** LogRequest Test for Synthetic Server ***\n");
  TSFuncRegister(TS_FID_OPTIONS_PROCESS);
  TSFuncRegister(TS_FID_OPTIONS_PROCESS_FINISH);
  TSFuncRegister(TS_FID_PLUGIN_FINISH);
  TSFuncRegister(TS_FID_RESPONSE_PREPARE);
}

void
TSOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "log_path") == 0) {
    if (strlen(value) < MAX_PATH_SIZE) {
      strcpy(my_plugin.log_path, value);
    } else {
      fprintf(stderr, "log_path size exceeds MAX_PATH_SIZE\n");
      exit(1);
    }
  }
}

void
TSOptionsProcessFinish()
{
  if (!(my_plugin.request_log = fopen(my_plugin.log_path, "a"))) {
    fprintf(stderr, "Error: Unable to open %s\n", my_plugin.log_path);
    exit(1);
  }
}

void
TSPluginFinish()
{
  fclose(my_plugin.request_log);
}


int
TSResponsePrepare(char *req_hdr, int req_len, void **response_id)
{
  char log_string[MAX_LOG_STR_SIZE + 1];
  char *time_string;
  int bytes_to_copy;
  time_t log_time;

  /*************** logging the requests **************/
  time(&log_time);
  time_string = ctime(&log_time);

  /* copy the time and status code into the log string */
  strcpy(log_string, time_string);
  if (strstr(req_hdr, "length")) {
    sprintf(log_string + strlen(time_string) - 1, " %s ", "200 OK");
  } else {
    sprintf(log_string + strlen(time_string) - 1, " %s ", "404 Not Found");
  }

  /* copy the name of document requested into the log string */
  bytes_to_copy = strchr(req_hdr + strlen("GET "), ' ') - req_hdr;
  if (strlen(log_string) + bytes_to_copy > MAX_LOG_STR_SIZE) {
    bytes_to_copy = MAX_LOG_STR_SIZE - strlen(log_string);
    fprintf(stderr, "Request in log will be truncated");
  }
  strncat(log_string, req_hdr, bytes_to_copy);
  fprintf(my_plugin.request_log, "%s\n", log_string);

  return FALSE;
}
