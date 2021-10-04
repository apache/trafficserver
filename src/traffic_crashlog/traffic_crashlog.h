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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/Diags.h"
#include "tscore/TextBuffer.h"

// ucontext.h is deprecated on Darwin, and we really only need it on Linux, so only
// include it if we are planning to use it.
#if defined(__linux__)
#include <ucontext.h>
#endif

// Printf format for crash log field labels.
#define LABELFMT "%-20s"

// Printf format for memory addresses.
#if SIZEOF_VOIDP == 8
#define ADDRFMT "0x%016" PRIx64
#define ADDRCAST(x) ((uint64_t)(x))
#elif SIZEOF_VOIDP == 4
#define ADDRFMT "0x%08" PRIx32
#define ADDRCAST(x) ((uint32_t)(x))
#else
#error unsupported pointer size
#endif

#define CRASHLOG_HAVE_THREADINFO 0x1u

struct crashlog_target {
  pid_t pid;
  siginfo_t siginfo;
#if defined(__linux__)
  ucontext_t ucontext;
#else
  char ucontext; // just a placeholder ...
#endif
  struct tm timestamp;
  unsigned flags;
};

bool crashlog_write_backtrace(FILE *, const crashlog_target &);
bool crashlog_write_datime(FILE *, const crashlog_target &);
bool crashlog_write_exename(FILE *, const crashlog_target &);
bool crashlog_write_proclimits(FILE *, const crashlog_target &);
bool crashlog_write_procname(FILE *, const crashlog_target &);
bool crashlog_write_procstatus(FILE *, const crashlog_target &);
bool crashlog_write_records(FILE *, const crashlog_target &);
bool crashlog_write_regions(FILE *, const crashlog_target &);
bool crashlog_write_registers(FILE *, const crashlog_target &);
bool crashlog_write_siginfo(FILE *, const crashlog_target &);
bool crashlog_write_uname(FILE *, const crashlog_target &);
