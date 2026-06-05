/** @file

  This file produces Unicorns and Rainbows for the ATS community

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

#include <sys/resource.h>
#include <cstdint>

rlim_t ink_get_fds_limit();
void   ink_set_fds_limit(rlim_t);
rlim_t ink_max_out_rlimit(int which);
rlim_t ink_get_max_files();

/** Get the current resident set size (RSS) of this process, in bytes.
 *
 * Unlike getrusage(2)'s ru_maxrss, this reports the *current* RSS rather than
 * the peak, so it is suitable for a live gauge and can both rise and fall.
 *
 * Implemented via /proc/self/statm on Linux, task_info() on macOS, and
 * sysctl(KERN_PROC_PID) on FreeBSD.
 *
 * @return current RSS in bytes, or 0 if it could not be determined.
 */
uint64_t ink_get_current_rss();
