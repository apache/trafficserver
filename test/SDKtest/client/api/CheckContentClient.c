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

/* CheckContentClient.c
 *
 *
 * Description:
 *   - Do sanity check on every byte in the responsed documents
 *     Note: This plugin needs to work with CheckContentServer 
 *           plugin for SDKtest_server
 */

#include "ClientAPI.h"
#include <stdio.h>
#include <stdlib.h>

void
INKPluginInit(int clientid)
{
  fprintf(stderr, "*** CheckContentClient Test for Client    ***\n");
  fprintf(stderr, "*** needs to work with CheckContentServer *** \n");
  INKFuncRegister(INK_FID_HEADER_PROCESS);
  INKFuncRegister(INK_FID_PARTIAL_BODY_PROCESS);
}


INKRequestAction
INKHeaderProcess(void *req_id, char *header, int length, char *request_str)
{
  return INK_KEEP_GOING;
}


INKRequestAction
INKPartialBodyProcess(void *req_id, void *partial_content, int partial_length, int accum_length)
{
  int i, code;

  /* check to see if the content in the 
   * buffer is what we expect ie. "01234567890123..."
   */
  i = partial_length;
  while (i > 0) {
    code = (accum_length - i) % 10 + '0';
    if (*((char *) partial_content + partial_length - i) != code) {
      fprintf(stderr, "Error: content is not correct\n");
      exit(1);
    }
    i--;
  }
  return INK_KEEP_GOING;
}
