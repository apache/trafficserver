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

/* partial_body_process.c
 *
 */

#include "ClientAPI.h"
#include <stdio.h>

void
INKPluginInit(int clientid)
{
  fprintf(stderr, "*** INKPartialBodyProcess Test for Client ***\n");
  INKFuncRegister(INK_FID_PARTIAL_BODY_PROCESS);
}


INKRequestAction
INKPartialBodyProcess(void *req_id, void *partial_content, int partial_length, int accum_length)
{
  return INK_KEEP_GOING;
}
