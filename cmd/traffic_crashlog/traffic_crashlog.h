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

#ifndef __TRAFFIC_CRASHLOG_H__
#define __TRAFFIC_CRASHLOG_H__

#include "libts.h"
#include "mgmtapi.h"

// Printf format for crash log field labels.
#define LABELFMT "%-20s"

// Printf format for memory addresses.
#define ADDRFMT "0x%016llx"

#define CRASHLOG_HAVE_THREADINFO 0x1u

struct crashlog_target
{
  pid_t       pid;
  siginfo_t   siginfo;
  ucontext_t  ucontext;
  struct tm   timestamp;
  unsigned    flags;
};

bool crashlog_write_backtrace(FILE *, const crashlog_target&);
bool crashlog_write_regions(FILE * , const crashlog_target&);
bool crashlog_write_exename(FILE *, const crashlog_target&);
bool crashlog_write_uname(FILE *, const crashlog_target&);
bool crashlog_write_datime(FILE *, const crashlog_target&);
bool crashlog_write_procname(FILE *, const crashlog_target&);
bool crashlog_write_procstatus(FILE *, const crashlog_target&);
bool crashlog_write_records(FILE *, const crashlog_target&);
bool crashlog_write_siginfo(FILE *, const crashlog_target&);
bool crashlog_write_registers(FILE *, const crashlog_target&);

#endif /* __TRAFFIC_CRASHLOG_H__ */
