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

/****************************************************************************

   Initialize.h --
   Created On      : Mon Feb  8 15:14:57 1999

   Description:



 ****************************************************************************/
#if !defined (_Initialize_h_)
#define _Initialize_h_

#define MAX_NUMBER_OF_THREADS  1024
#define DIAGS_LOG_FILE         "diags.log"

extern Diags *diags;
extern int fds_limit; // TODO: rename
extern int system_num_of_net_threads;
extern int system_syslog_facility;

void init_system_settings(void);
void init_system_dirs(void);
void init_system_core_size(void);
void init_system_syslog_log_configure(void);
//void init_system_logging();
void init_system_reconfigure_diags(void);
void init_system_diags(char *bdt, char *bat);

void init_system_adjust_num_of_net_threads(void);

//void initialize_standalone();

#endif
