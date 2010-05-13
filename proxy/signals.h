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

/**************************************************************************
  Signal functions and handlers.

**************************************************************************/

#ifndef _signals_h_
#define _signals_h_

/*
 *  Global data
 */

extern int exited_children;
typedef void (*sig_callback_fptr) (int signo);
/*
*  plugins use this to attach clean up handlers
*  for SIGSEGV and SIGBUS
*  Return value: 0 on success, -1 on failure
*/
int register_signal_callback(sig_callback_fptr f);

/*
 *  Function prototypes
 */

void init_signals(bool do_stackdump=true);
void init_signals2();
void init_daemon_signals();

#endif /* _signals_h_ */
