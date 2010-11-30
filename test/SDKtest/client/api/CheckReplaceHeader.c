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

/* CheckReplaceHeader.c
 *
 *
 * Description:
 *   - Checks the response-header that is received from the proxy to see if it has
 *     the correct MIME header-field(ACCEPT-RANGES) with its correct value.
 * Designed to test replace-header plugin under load.
 *
 * Make sure that replace-header plugin is loaded on the TS-proxy used.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include "ClientAPI.h"

#define TRUE 1
#define FALSE 0

int replace_hdr_test_failed;

void
TSPluginInit(int clientid)
{

  replace_hdr_test_failed = 0;

  fprintf(stderr, "*** CheckReplaceHeader Test for replace-header-plugin ***\n");
  TSFuncRegister(TS_FID_HEADER_PROCESS);
}


TSRequestAction
TSHeaderProcess(void *req_id, char *header, int length, char *request_str)
{

  char *accept_ranges_loc;

  accept_ranges_loc = (char *) strstr(header, "none");
  if (accept_ranges_loc == NULL) {

    if (replace_hdr_test_failed == 0) {
      fprintf(stdout, "SDKtest: replace-header-Test Failed: Accept-Ranges field error\n");
      /*fprintf(stdout, "Response header is:\n%s\n", header); */
    }
    replace_hdr_test_failed = (replace_hdr_test_failed + 1) % 200;
    return TS_KEEP_GOING;
  }

  while (*accept_ranges_loc != ':')
    accept_ranges_loc--;

  while (*accept_ranges_loc == ' ' || *accept_ranges_loc == '\t')
    accept_ranges_loc--;

  accept_ranges_loc -= strlen("Accept-Ranges");

  if (strncasecmp(accept_ranges_loc, "Accept-Ranges", 13)) {

    if (replace_hdr_test_failed == 0) {
      fprintf(stdout, "SDKtest: replace-header-Test FAILED: Accept-Ranges field error\n");
      /*fprintf(stdout, "Response header is:\n%s\n", header); */
    }
    replace_hdr_test_failed = (replace_hdr_test_failed + 1) % 200;
    return TS_KEEP_GOING;
  }

  /*fprintf(stdout, "Test PASSED!!!\n"); */

  return TS_STOP_SUCCESS;
}
