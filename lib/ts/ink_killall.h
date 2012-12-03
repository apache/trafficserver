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

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


#ifndef _INK_KILLALL_H_
#define _INK_KILLALL_H_

#include "ink_config.h"

#if defined(linux)

/*-------------------------------------------------------------------------
   ink_killall
   - Sends signal 'sig' to all processes with the name 'pname'
   - Returns: -1 error
               0 okay
  -------------------------------------------------------------------------*/
int ink_killall(const char *pname, int sig);

/*-------------------------------------------------------------------------
   ink_killall_get_pidv_xmalloc
   - Get all pid's named 'pname' and stores into ats_malloc'd
     pid_t array, 'pidv'
   - Returns: -1 error (pidv: set to NULL; pidvcnt: set to 0)
               0 okay (pidv: ats_malloc'd pid vector; pidvcnt: number of pid's;
	               if pidvcnt is set to 0, then pidv will be set to NULL)
  -------------------------------------------------------------------------*/
int ink_killall_get_pidv_xmalloc(const char *pname, pid_t ** pidv, int *pidvcnt);

/*-------------------------------------------------------------------------
   ink_killall_kill_pidv
   - Kills all pid's in 'pidv' with signal 'sig'
   - Returns: -1 error
               0 okay
  -------------------------------------------------------------------------*/
int ink_killall_kill_pidv(pid_t * pidv, int pidvcnt, int sig);

#endif

#endif
