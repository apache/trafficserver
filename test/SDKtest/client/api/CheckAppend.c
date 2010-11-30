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
 *   use_live_url: 1 = use live url 0 = use SDKtest server
 *   url_file: full path of the file containing the list of urls to be used.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include "ClientAPI.h"
#include <assert.h>
#include <ctype.h>

#define MAX_PATH_SIZE 256
#define TRUE 1
#define FALSE 0
/*#define PRINT_DEBUG*/
#define MAX_URL_LEN 1024


#ifdef PRINT_DEBUG
#define print_debug(x) fprintf x
#else
#define print_debug(x) ;
#endif

typedef struct
{
  char *tail_of_resp;
  int tail_of_resp_index;
  int check_this_response;
} CONN_DATA;


typedef struct
{
  /* for reporting stats */
  long requests;
  long successful_requests;
  long unfinished_requests;
  long total_bytes_received;

  /* for the live URL file */
  FILE *url_file;
  /* vars related to checking appended content */

  char append_file_path[MAX_PATH_SIZE];
  int append_file;
  int go_direct;
  struct stat statbuf;
  char *append_content;
  char *tmpBuf;
  int append_len;
  int url_file_present;
} CheckAppendPlugin;

CheckAppendPlugin my_plugin;

void
TSPluginInit(int client_id)
{
  my_plugin.requests = 0;
  my_plugin.successful_requests = 0;
  my_plugin.go_direct = 0;
  my_plugin.url_file_present = 0;

  fprintf(stderr, "*** CheckAppend Test for append-transform-plugin v1.0***\n");
  /* register the plugin functions that are called back
   * later in the program */
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
TSConnectionFinish(void *req_id, TSConnectionStatus conn_status)
{
  if (conn_status == TS_TIME_EXPIRE)
    my_plugin.unfinished_requests++;
  free(((CONN_DATA *) req_id)->tail_of_resp);
  free(req_id);
}

/* process options specified in SDKtest_client.config */
void
TSOptionsProcess(char *option, char *value)
{
  if (strcmp(option, "url_file") == 0) {        /* get the live URL file here */
    if (!(my_plugin.url_file = fopen(value, "r"))) {
      fprintf(stderr, "ERROR: could not open the url_file: %s\n", value);
      perror("ERROR: URL file open");
      exit(1);
    } else {
      my_plugin.url_file_present = 1;
    }
  } else if (strcmp(option, "append-file-path") == 0) {
    if (strlen(value) < MAX_PATH_SIZE) {
      strcpy(my_plugin.append_file_path, value);
    } else {
      fprintf(stderr, "append-file-path size exceeds MAX_PATH_SIZE\n");
      exit(1);
    }
  } else if (strcmp(option, "use_live_url") == 0) {
    if (strcmp(value, "1") == 0) {
      my_plugin.go_direct = 1;
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
    fprintf(stderr, "Error: malloc error for append_content\n");
    exit(1);
  }

  my_plugin.tmpBuf = (char *) malloc(sizeof(char) * (my_plugin.append_len + 1));
  if (my_plugin.tmpBuf == NULL) {
    close(my_plugin.append_file);
    fprintf(stderr, "Error: malloc error for tmpBuf\n");
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

void
TSPluginFinish()
{
  int i;

  if (my_plugin.url_file_present) {
    fclose(my_plugin.url_file);
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
TSRequestCreate(char *origin_server_host /* return */ , int max_hostname_size,
                 char *origin_server_port /* return */ , int max_portname_size,
                 char *request_buf /* return */ , int max_request_size,
                 void **req_id /* return */ )
{
  CONN_DATA *user;
  int i;
  char url_line[MAX_URL_LEN];

  /* SDKtest in "live_url" mode */
  if (my_plugin.go_direct) {
    if (my_plugin.url_file_present == 0) {
      fprintf(stdout, "CheckAppend: invalid URL file specified... exiting..\n");
      return FALSE;
    }
    if (fscanf(my_plugin.url_file, "%s", url_line) == EOF) {
      fprintf(stdout, "CheckAppend-1: URL file exhausted... ending test\n");
      return FALSE;
    }

    print_debug((stdout, "url_line = %s\n", url_line));
    sprintf(request_buf, "GET %s HTTP/1.1\r\nAccept: */*\r\n\r\n", url_line);
  }
  user = (CONN_DATA *) malloc(sizeof(CONN_DATA));

  if (user == NULL) {
    fprintf(stderr, "ERROR!! NOT ENOUGH MEMORY for conn_data\n");
    return FALSE;
  }

  user->tail_of_resp = (char *) malloc(sizeof(char) * (my_plugin.append_len + 1));
  if (user->tail_of_resp == NULL) {
    fprintf(stderr, "Error: malloc error\n");
    return FALSE;
  }

  user->check_this_response = 1;

  user->tail_of_resp_index = 0;
  for (i = 0; i < my_plugin.append_len + 1; i++) {
    user->tail_of_resp[i] = 'Q';
  }

  *req_id = (void *) user;
  my_plugin.requests++;

  return TRUE;
}

/* process response header returned either from synthetic
 * server or from proxy server
 */
TSRequestAction
TSHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  char *content_type;
  CONN_DATA *p_conn = (CONN_DATA *) req_id;

  content_type = strstr(header, "Content-type");

  if (content_type != NULL) {
    content_type += 12;

    while (*content_type == ' ')
      content_type++;
    if (*content_type == ':')
      content_type++;
    while (*content_type == ' ')
      content_type++;

    if (strncasecmp(content_type, "text", 4) == 0) {
      p_conn->check_this_response = 1;
    } else {
      p_conn->check_this_response = 0;
    }
  } else {

/* No content-type present for this response. Dont trust this and
   assume this one carries non-text data. */

    p_conn->check_this_response = 0;
  }

  return TS_KEEP_GOING;
}


TSRequestAction
TSPartialBodyProcess(void *request_id, void *partial_content, int partial_length, int accum_length)
{
  char *src;
  int room, bytes_to_move;
  char *last_byte;
  CONN_DATA *req_id;

  req_id = (CONN_DATA *) request_id;
  if (req_id == NULL || (req_id->check_this_response == 0))
    return TS_STOP_FAIL;

  /*print_debug((stderr, "req_id %x accum_length %d", req_id, accum_length));
     print_debug((stderr, "Rx response %d bytes\n", partial_length)); */

  if (partial_length == 0) {    /* Response is now complete. */

    if (memcmp(req_id->tail_of_resp, my_plugin.append_content, my_plugin.append_len) == 0) {

      return TS_STOP_SUCCESS;
    }

    fprintf(stdout, "TEST_FAILED: appended content doesn't match for req_id %x\n", req_id);
    print_debug((stderr, "append: [%s] tail_of_resp [%s]\n", my_plugin.append_content, req_id->tail_of_resp));
    return TS_STOP_FAIL;
  }

/* Response not complete. Copy last _useful_ bytes to tail_of_file buffer */

  last_byte = (char *) partial_content + partial_length;

  if (partial_length > my_plugin.append_len)
    partial_length = my_plugin.append_len;

  src = last_byte - partial_length;

/* append_at_end(src, partial_length); */

  room = my_plugin.append_len - req_id->tail_of_resp_index;     /* how much room we have */

  if (room < partial_length) {

/* we dont have enough room. Need to create room for <partial_length - room> bytes */

    bytes_to_move = req_id->tail_of_resp_index - (partial_length - room);
    memmove(req_id->tail_of_resp, req_id->tail_of_resp + partial_length - room, bytes_to_move);
    req_id->tail_of_resp_index -= partial_length - room;
  }

  memcpy(req_id->tail_of_resp + req_id->tail_of_resp_index, src, partial_length);

  req_id->tail_of_resp_index += partial_length;
  req_id->tail_of_resp[req_id->tail_of_resp_index + 1] = '\0';

  return TS_KEEP_GOING;

}


/* output report data after the SDKtest report */
void
TSReport()
{
  TSReportSingleData("Total Requests", "count", TS_SUM, (double) my_plugin.requests);
  TSReportSingleData("Successful Documents", "count", TS_SUM, (double) my_plugin.successful_requests);
  TSReportSingleData("Unfinished Documents", "count", TS_SUM, (double) my_plugin.unfinished_requests);
  TSReportSingleData("Total Bytes Received", "count", TS_SUM, (double) my_plugin.total_bytes_received);
}
